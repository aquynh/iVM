/*
 * H2FMI emulation
 *
 * by cmw (portions by ricky26)
 */

#include "hw.h"
#include "flash.h"
#include "sysbus.h"
#include "s5l8930.h"
#include "block.h"
#include "block_int.h"
#include <strings.h>

#define DNAND(x) x

#define H2FMI_MAX_CHIPS   16
#define H2FMI_CHIPID_LENGTH 8

static uint32_t h2fmi_hash_table[256];

static void initNandHash(void)
{
    unsigned int i;
    unsigned int val = 0x50F4546A;
    for(i = 0; i < 256; i++)
    {
        val = (0x19660D * val) + 0x3C6EF35F;

        int j;
        for(j = 1; j < 763; j++)
        {
            val = (0x19660D * val) + 0x3C6EF35F;
        }

        h2fmi_hash_table[i] = val;
    }
}

// h2fmi_buffer
//
// Used to simulate buffers in the H2FMI
// hardware.

struct h2fmi_buffer
{
	void *ptr;
	size_t read, written;
	size_t size;
};

static inline void h2fmi_buffer_init(struct h2fmi_buffer *_buf)
{
	_buf->ptr = NULL;
}

static inline void h2fmi_buffer_free(struct h2fmi_buffer *_buf)
{
	if(_buf->ptr)
		qemu_free(_buf->ptr);
}

static int h2fmi_buffer_alloc(struct h2fmi_buffer *_buf, size_t _sz)
{
	if(!_sz)
	{
		_buf->ptr = 0;
		return 0;
	}

	_buf->ptr = qemu_mallocz(_sz);
	if(!_buf->ptr)
		return -ENOMEM;

	_buf->read = 0;
	_buf->written = 0;
	_buf->size = _sz;
	return 0;
}

static int h2fmi_buffer_realloc(struct h2fmi_buffer *_buf, size_t _sz)
{
	void *ptr;
	size_t sz;

	if(!_buf->ptr || _buf->size > _sz)
		return 0;

	sz = ((_sz + 512 - 1)/512)*512;
	ptr = qemu_realloc(_buf->ptr, sz);
	if(!ptr)
		return -ENOMEM;

	_buf->ptr = ptr;
	_buf->size = sz;
	return 0;
}

static inline void h2fmi_buffer_clear(struct h2fmi_buffer *_buf)
{
	_buf->read = 0;
	_buf->written = 0;
}

static int h2fmi_buffer_read(struct h2fmi_buffer *_buf, void *_ptr, size_t _sz)
{
	size_t amt;

	if(!_buf->ptr || !_buf->written || _buf->read >= _buf->written)
		return 0;

	if((_buf->written - _buf->read) < _sz)
		amt = _buf->written - _buf->read;
	else
		amt = _sz;

	memcpy(_ptr, _buf->ptr + _buf->read, amt);
	_buf->read += _sz;
	return amt;
}

static int h2fmi_buffer_write(struct h2fmi_buffer *_buf, const void *_ptr, size_t _sz)
{
	int ret;

	if(!_buf->ptr)
	{
		ret = h2fmi_buffer_alloc(_buf, _sz);
		if(ret)
			return ret;

		memcpy(_buf->ptr, _ptr, _sz);
		_buf->written = _sz;
		return _sz;
	}

	if((_buf->written + _sz) > _buf->size)
	{
		ret = h2fmi_buffer_realloc(_buf, _buf->written + _sz);
		if(ret)
			return ret;
	}

	memcpy(_buf->ptr + _buf->written, _ptr, _sz);
	_buf->written += _sz;
	return _sz;
}

static void *h2fmi_buffer_map(struct h2fmi_buffer *_buf, size_t _sz)
{
	int ret;
	void *ptr;

	if(!_buf->ptr)
	{
		ret = h2fmi_buffer_alloc(_buf, _sz);
		if(ret)
			return NULL;

		_buf->written = _sz;
		return _buf->ptr;
	}

	if((_buf->written + _sz) > _buf->size)
	{
		ret = h2fmi_buffer_realloc(_buf, _buf->written + _sz);
		if(ret)
			return NULL;
	}

	ptr = _buf->ptr + _buf->written;
	_buf->written += _sz;
	return ptr;
}

typedef struct
{
    SysBusDevice busdev;
    qemu_irq irq;
	BlockDriverState ce[H2FMI_MAX_CHIPS];
	int bitmap;

	char *ce_paths;
	uint32_t page_size, meta_size;
	uint32_t ecc_shift, ecc_stages;

	unsigned eccfmt, pagefmt;
	unsigned timing;

	uint32_t addr;
	uint32_t chip;

    uint32_t nsts, csts, ests;
	uint32_t nstatus;

	uint16_t ncmd;
	uint8_t ccmd;
	
	uint32_t fmtn;

	struct h2fmi_buffer buf0;
	struct h2fmi_buffer buf1;
	struct h2fmi_buffer eccbuf;
} h2fmi_state_t;

static inline uint8_t h2fmi_active_chip(h2fmi_state_t *_state)
{
	//fprintf(stderr, "%s: active chip ptr %p fmt %d\n", __FUNCTION__, _state, _state->fmtn);
	if(!_state->chip)
		return -1;

	//fprintf(stderr, "%s: chip: %d state: 0x%08x fmt %d\n", __FUNCTION__, ffs(_state->chip) - 1, _state->chip, _state->fmtn);
	return ffs(_state->chip) - 1;
}

static inline int64_t h2fmi_id_offset(h2fmi_state_t *_state, int _id)
{
	if(_id)
		return -1;

	return 0;
}

static inline int64_t h2fmi_page_offset(h2fmi_state_t *_state, int _addr)
{
	return H2FMI_CHIPID_LENGTH + _addr*(1ull + _state->page_size + _state->meta_size);
}

/* Flash CMDS */
#define NAND_CMD_READID		0x90
#define NAND_CMD_READ0		0x00
#define NAND_CMD_READSTART	0x30
#define NAND_CMD_ERASE		0x60
#define NAND_CMD_WRITE0		0x10
#define NAND_CMD_WRITE1		0x11

/* Base h2fmi */
#define H2FMI_CBASE			(0x0)
#define H2FMI_ECCFMT		(0x0)
#define H2FMI_CCMD			(0x4)
#define H2FMI_CCMDSTS		(0x8)
#define H2FMI_CSTS			(0xC)
#define H2FMI_CREQ			(0x10)
#define H2FMI_DATA0			(0x14)
#define H2FMI_DATA1			(0x18)
#define H2FMI_CDMASTS		(0x1C)
#define H2FMI_PAGEFMT		(0x34)

/* Flash regs */
#define H2FMI_NBASE			(0x40000)
#define H2FMI_RESET			(0x0)
#define H2FMI_TIMING		(0x8)
#define H2FMI_CHIP_MASK		(0xC)
#define H2FMI_NREQ			(0x10)
#define H2FMI_NCMD			(0x14)
#define H2FMI_ADDR0			(0x18)
#define H2FMI_ADDR1			(0x1C)
#define H2FMI_ADDRMODE		(0x20)
#define H2FMI_UNKREG8		(0x24)
#define H2FMI_UNK440		(0x40)
#define H2FMI_NSTS			(0x44)
#define H2FMI_STATUS		(0x48)
#define H2FMI_UNK44C		(0x4C)

/* ECC regs */
#define H2FMI_EBASE			(0x80000)
#define H2FMI_ECCCFG		(0x8)
#define H2FMI_ECCBUF		(0xC)
#define H2FMI_ECCSTS		(0x10)
#define H2FMI_ECCINT		(0x14)

static int h2fmi_add_chip(h2fmi_state_t *_h2fmi, int _ce, const char *_file)
{
	int ret;
	if(_ce > H2FMI_MAX_CHIPS)
	{
		//fprintf(stderr, "invalid CE %d.\n", _ce);
		return -EINVAL;
	}

	if(_h2fmi->bitmap & (1 << _ce))
	{
		bdrv_close(&_h2fmi->ce[_ce]);
		_h2fmi->bitmap &=~ (1 << _ce);
	}

	//fprintf(stderr, "FILE: %s\n", _file);
	ret = bdrv_open(&_h2fmi->ce[_ce], _file, BDRV_O_RDWR, NULL);
	if(ret)
		return ret;

	//fprintf(stderr, "added h2fmi ce%d, %s.\n", _ce, _file);
	_h2fmi->bitmap |= (1 << _ce);
	return 0;
}

static int h2fmi_setup_chips(h2fmi_state_t *_h2fmi, const char *_paths)
{
	char *str = strdup(_paths);
	char *last = str;
	char *end = last + strlen(_paths);
	char *ptr;
	uint32_t ce = 0;
	int ret = 0;
	int hasce = 0;

	for(ptr = last; ptr < end; ptr++)
	{
		if(!hasce && *ptr == ',')
		{
			*ptr = 0;
			ce = atoi(last);
			last = ptr+1;
			hasce = 1;
		}
		else if(*ptr == ';')
		{
			*ptr = 0;
			ret = h2fmi_add_chip(_h2fmi, ce, last);
			if(ret)
				break;

			ce++;
			hasce = 0;
			last = ptr+1;
		}
	}

	if(!ret && last < end)
		ret = h2fmi_add_chip(_h2fmi, ce, last);

	free(str);
	return ret;
}

static void h2fmi_do_ccmd(h2fmi_state_t *_h2fmi, uint32_t _v)
{
	switch(_v)
	{
	case 6:
		h2fmi_buffer_clear(&_h2fmi->buf0);
		h2fmi_buffer_clear(&_h2fmi->buf1);
		break;
	}
}

static int iopEnabled = 0;

void enableIOPH2fmi(void) {
	iopEnabled = 1;
}

static int h2fmi_do_readid(h2fmi_state_t *_h2fmi, int _ce)
{
	void *data = h2fmi_buffer_map(&_h2fmi->buf0, H2FMI_CHIPID_LENGTH);
	int ret = bdrv_pread(&_h2fmi->ce[_ce], 0, // h2fmi_id_offset(_h2fmi, _h2fmi->addr),
			data, H2FMI_CHIPID_LENGTH);
	//fprintf(stderr, "%s: data 0x%08x ret: %d ptr %p fmt %d offset 0x%08x\n", __FUNCTION__, *(uint32_t *)data, ret, _h2fmi, _h2fmi->fmtn,  h2fmi_id_offset(_h2fmi, _h2fmi->addr));
	if(ret <= 0)
		h2fmi_buffer_clear(&_h2fmi->buf0);
	return ret;
}

static int h2fmi_do_erase(h2fmi_state_t *_h2fmi, int _ce)
{

    //int bdrv_pwrite(BlockDriverState *bs, int64_t offset, const void *buf, int count1)
	int buf = '1';
	int ret = 1;
	//int ret = bdrv_pwrite(&_h2fmi->ce[_ce], offset, 1);
	fprintf(stderr, "%s: returned %d for address 0x%08x on ce %d\n", __FUNCTION__, ret, _h2fmi->addr, _ce);
	if(ret < 0)
		return ret;
	_h2fmi->nstatus = 0x80;

	return 0;
}

static int h2fmi_do_read(h2fmi_state_t *_h2fmi, int _ce)
{
	uint32_t err = 4; // 0xFE is empty
	int i;
	uint8_t marker;
	void *data, *meta;
	int ret = bdrv_pread(&_h2fmi->ce[_ce], h2fmi_page_offset(_h2fmi, _h2fmi->addr),
			&marker, 1);

	fprintf(stderr, "%s: returned %d for address 0x%08x and page offset %llx on ce %d fmt %d\n", __FUNCTION__, ret, _h2fmi->addr, h2fmi_page_offset(_h2fmi, _h2fmi->addr), _ce, _h2fmi->fmtn);
	if(ret <= 0)
	{
		fprintf(stderr, "%s: failed to read page marker for %d.\n",__FUNCTION__, _h2fmi->addr);
		goto done;
	}

	if(!marker || marker == '0')
	{
		// empty
		//err = 0x2;
		err = 0xa;
		goto done;
	}

	data = h2fmi_buffer_map(&_h2fmi->buf0, _h2fmi->page_size);
	meta = h2fmi_buffer_map(&_h2fmi->buf1, _h2fmi->meta_size-2);
	if(!meta || !data)
	{
		fprintf(stderr, "%s: failed to map buffers.\n", __FUNCTION__);
		goto done;
	}

	ret = bdrv_pread(&_h2fmi->ce[_ce], h2fmi_page_offset(_h2fmi, _h2fmi->addr) + 1,
			data, _h2fmi->page_size);
	if(ret <= 0)
	{
		fprintf(stderr, "%s: failed to read page ret %d.\n", __FUNCTION__, ret);
		goto done;
	}

	ret = bdrv_pread(&_h2fmi->ce[_ce], h2fmi_page_offset(_h2fmi, _h2fmi->addr)
			+ 1 + _h2fmi->page_size, meta, _h2fmi->meta_size-2);
	if(ret <= 0)
	{
		fprintf(stderr, "%s: failed to read meta. ret: %d\n", __FUNCTION__, ret);
		goto done;
	}

    fprintf(stderr, "%02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x.\n", ((uint8_t*)meta)[0], ((uint8_t*)meta)[1], ((uint8_t*)meta)[2], ((uint8_t*)meta)[3],
        ((uint8_t*)meta)[4], ((uint8_t*)meta)[5], ((uint8_t*)meta)[6], ((uint8_t*)meta)[7], ((uint8_t*)meta)[8], ((uint8_t*)meta)[9], ((uint8_t*)meta)[10], ((uint8_t*)meta)[11]);

   	for(i = 0; i < 3; i++)
   		((uint32_t*)meta)[i] ^= h2fmi_hash_table[(i + _h2fmi->addr) % ARRAY_SIZE(h2fmi_hash_table)];

    fprintf(stderr, "%02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x.\n", ((uint8_t*)meta)[0], ((uint8_t*)meta)[1], ((uint8_t*)meta)[2], ((uint8_t*)meta)[3],
        ((uint8_t*)meta)[4], ((uint8_t*)meta)[5], ((uint8_t*)meta)[6], ((uint8_t*)meta)[7], ((uint8_t*)meta)[8], ((uint8_t*)meta)[9], ((uint8_t*)meta)[10], ((uint8_t*)meta)[11]);


	err = 0;
done:
	for(i = 0; i < _h2fmi->ecc_stages; i++)
		h2fmi_buffer_write(&_h2fmi->eccbuf, &err, 4);

	return ret;
}

static void h2fmi_do_single_ncmd(h2fmi_state_t *_h2fmi, uint8_t _cmd)
{
	int ce = h2fmi_active_chip(_h2fmi);

	fprintf(stderr, "got active chip %d\n", ce);
	if(ce < 0 || ce >= H2FMI_MAX_CHIPS || !(_h2fmi->bitmap & (1 << ce))) {
		fprintf(stderr, "error with chip id %d\n", ce);
		return;
	}

	fprintf(stderr, "%s: ce 0x%08x - _h2fmi->bitmap 0x%08x fmt %d cmd %d\n", __FUNCTION__, ce, _h2fmi->bitmap, _h2fmi->fmtn, _cmd);
	if(!_cmd) // DEPLETE or READ0, we don't care.
		return;

	fprintf(stderr, "%s: cmd 0x%08x\n", __FUNCTION__, _cmd);
	switch(_cmd)
	{
	case NAND_CMD_READID:
		h2fmi_do_readid(_h2fmi, ce);
		break;

	case NAND_CMD_READSTART:
		h2fmi_do_read(_h2fmi, ce);
		break;

	case NAND_CMD_ERASE:
		h2fmi_do_erase(_h2fmi, ce);
		_h2fmi->nstatus |= 0x80;
	    if(iopEnabled)
			qemu_irq_raise(_h2fmi->irq);
		break;
	}
}

static void h2fmi_do_ncmd(h2fmi_state_t *_h2fmi, uint32_t _v)
{
	while(_v)
	{
		h2fmi_do_single_ncmd(_h2fmi, _v & 0xFF);
		_v >>= 8;
	}
}

// Controller Registers MMIO
static uint32_t h2fmi_creadl(void *_op, target_phys_addr_t _addr)
{
	uint32_t ret = 0;
	h2fmi_state_t *h2fmi = _op;

	if(_addr != H2FMI_DATA0 && _addr != H2FMI_DATA1)
    	fprintf(stderr, "%s: offset 0x%08x fmt %d\n", __FUNCTION__, _addr, h2fmi->fmtn);

	switch(_addr)
	{
	case H2FMI_CCMD:
		return 0;

	case H2FMI_CSTS:
		return h2fmi->csts;

	case H2FMI_CCMDSTS:
		return 0;

	case H2FMI_CDMASTS:
		return 0x18; // Means DMA is done, which is fine by us. :P

	case H2FMI_PAGEFMT:
		return h2fmi->pagefmt;

	case H2FMI_ECCFMT:
		return h2fmi->eccfmt;

	case H2FMI_DATA0:
		h2fmi_buffer_read(&h2fmi->buf0, &ret, sizeof(ret));
		//fprintf(stderr, "%s: data0: 0x%08x\n", __FUNCTION__, ret);
		return ret;

	case H2FMI_DATA1:
		h2fmi_buffer_read(&h2fmi->buf1, &ret, sizeof(ret));
        //fprintf(stderr, "%s: data1: 0x%08x\n", __FUNCTION__, ret);
		return ret;
	}

	return 0;
}

static uint32_t h2fmi_creads(void *_op, target_phys_addr_t _addr)
{
	uint16_t ret = 0;
	h2fmi_state_t *h2fmi = _op;

	switch(_addr)
	{
	case H2FMI_DATA0:
		h2fmi_buffer_read(&h2fmi->buf0, &ret, sizeof(ret));
		//fprintf(stderr, "%s: data0: 0x%08x\n", __FUNCTION__, ret);
		return ret;

	case H2FMI_DATA1:
		h2fmi_buffer_read(&h2fmi->buf1, &ret, sizeof(ret));
        //fprintf(stderr, "%s: data1: 0x%08x\n", __FUNCTION__, ret);
		return ret;
	}

	return 0;
}

static uint32_t h2fmi_creadb(void *_op, target_phys_addr_t _addr)
{
	uint8_t ret = 0;
	h2fmi_state_t *h2fmi = _op;

	switch(_addr)
	{
	case H2FMI_DATA0:
		h2fmi_buffer_read(&h2fmi->buf0, &ret, sizeof(ret));
		//fprintf(stderr, "%s: data0: 0x%08x\n", __FUNCTION__, ret);
		return ret;

	case H2FMI_DATA1:
		h2fmi_buffer_read(&h2fmi->buf1, &ret, sizeof(ret));
        //fprintf(stderr, "%s: data1: 0x%08x\n", __FUNCTION__, ret);
		return ret;
	}

	return 0;
}

static void h2fmi_cwritel(void *_op, target_phys_addr_t _addr, uint32_t _v)
{
	h2fmi_state_t *h2fmi = _op;

    //if(_addr != 0x14)
    	fprintf(stderr, "%s: offset 0x%08x value 0x%08x fmt %d\n", __FUNCTION__, _addr, _v, h2fmi->fmtn);

	switch(_addr)
	{
	case H2FMI_CCMD:
		if(_v == 3) 
			h2fmi->csts |= 2;
		if(_v == 6) // reset
			h2fmi->addr = 0;

		h2fmi_do_ccmd(h2fmi, _v);
		break;

	case H2FMI_CREQ:
		h2fmi->csts |= _v;
		// TODO: flag interrupt.
		if(iopEnabled)
			qemu_irq_raise(h2fmi->irq);
		break;

	case H2FMI_CSTS:
		h2fmi->csts &=~ _v;
		if(iopEnabled)
			qemu_irq_lower(h2fmi->irq);
		// TODO: clear interrupt;
		break;

	case H2FMI_DATA0:
		h2fmi_buffer_write(&h2fmi->buf0, &_v, sizeof(_v));
		break;

	case H2FMI_DATA1:
		h2fmi_buffer_write(&h2fmi->buf1, &_v, sizeof(_v));
		break;
	}
}

// NAND Registers MMIO
static uint32_t h2fmi_nreadl(void *_op, target_phys_addr_t _addr)
{
	uint32_t ret = 0;
	h2fmi_state_t *h2fmi = _op;

    fprintf(stderr, "%s: offset 0x%08x fmt %d\n", __FUNCTION__, _addr, h2fmi->fmtn);

	switch(_addr)
	{
	case H2FMI_CHIP_MASK:
		return h2fmi->chip;

	case H2FMI_NSTS:
		return h2fmi->nsts;

	case H2FMI_ADDR0:
		return h2fmi->addr << 16;

	case H2FMI_ADDR1:
		return h2fmi->addr >> 16;

	case H2FMI_TIMING:
		return h2fmi->timing;

	case H2FMI_STATUS:
		return h2fmi->nstatus;
	}

	return ret;
}

static void h2fmi_nwritel(void *_op, target_phys_addr_t _addr, uint32_t _v)
{
	h2fmi_state_t *h2fmi = _op;

	fprintf(stderr, "%s: offset 0x%08x value 0x%08x h2fmi ptr %p fmt %d\n", __FUNCTION__, _addr, _v, h2fmi, h2fmi->fmtn);
	switch(_addr)
	{
	case H2FMI_CHIP_MASK:
		h2fmi->chip = _v;
		break;

	case H2FMI_NREQ:
		if((h2fmi->nstatus) && (!_v))
			h2fmi->nstatus = _v;

		h2fmi->nsts |= _v;

		if(iopEnabled)
			qemu_irq_raise(h2fmi->irq);
		// TODO: flag interrupt.
		break;

	case H2FMI_NSTS:
		h2fmi->nsts &=~ _v;

		if(iopEnabled)
			qemu_irq_lower(h2fmi->irq);
		// TODO: clear interrupt.
		break;

	case H2FMI_NCMD:
		h2fmi_do_ncmd(h2fmi, _v);
		break;

	case H2FMI_ADDR0:
		h2fmi->addr = (h2fmi->addr &~ 0xFFFF) | (_v >> 16);
        fprintf(stderr, "%s: 0x18 got addr %x.\n", __func__, h2fmi->addr);
		break;

	case H2FMI_ADDR1:
		h2fmi->addr = (h2fmi->addr & 0xFFFF) | (_v << 16);
		fprintf(stderr, "%s: 0x1c got addr %x.\n", __func__, h2fmi->addr);
		break;

	case H2FMI_TIMING:
		h2fmi->timing = _v;
		break;

	case H2FMI_STATUS:
		h2fmi->nstatus = _v;
		break;
	}
}

static uint32_t h2fmi_ereadl(void *_op, target_phys_addr_t _addr)
{
	uint32_t ret = 0;
	h2fmi_state_t *h2fmi = _op;

    //fprintf(stderr, "%s: offset 0x%08x fmt %d\n", __FUNCTION__, _addr, h2fmi->fmtn);

	switch(_addr)
	{
	case H2FMI_ECCSTS:
		return h2fmi->ests;

	case H2FMI_ECCBUF:
		h2fmi_buffer_read(&h2fmi->eccbuf, &ret, sizeof(ret));
		//fprintf(stderr, "%s: eccbuf 0x%08x\n", __FUNCTION__, ret);
		break;
	}

	return ret;
}

static void h2fmi_ewritel(void *_op, target_phys_addr_t _addr, uint32_t _v)
{
	h2fmi_state_t *h2fmi = _op;

    //fprintf(stderr, "%s: offset 0x%08x value 0x%08x fmt %d\n", __FUNCTION__, _addr, _v, h2fmi->fmtn);

	switch(_addr)
	{
	case H2FMI_ECCSTS:
		h2fmi->ests &=~ _v;
		break;

	case H2FMI_ECCBUF:
		h2fmi_buffer_clear(&h2fmi->eccbuf);
		break;
	}
}

// Controller Memory Funcs
static CPUReadMemoryFunc *const h2fmi_cread[] = {
	&h2fmi_creadb,
	&h2fmi_creads,
	&h2fmi_creadl,
};

static CPUWriteMemoryFunc *const h2fmi_cwrite[] = {
	&h2fmi_cwritel,
	&h2fmi_cwritel,
	&h2fmi_cwritel,
};

// NAND Memory Funcs
static CPUReadMemoryFunc *const h2fmi_nread[] = {
	&h2fmi_nreadl,
	&h2fmi_nreadl,
	&h2fmi_nreadl,
};

static CPUWriteMemoryFunc *const h2fmi_nwrite[] = {
	&h2fmi_nwritel,
	&h2fmi_nwritel,
	&h2fmi_nwritel,
};

// ECC Memory Funcs
static CPUReadMemoryFunc *const h2fmi_eread[] = {
	&h2fmi_ereadl,
	&h2fmi_ereadl,
	&h2fmi_ereadl,
};

static CPUWriteMemoryFunc *const h2fmi_ewrite[] = {
	&h2fmi_ewritel,
	&h2fmi_ewritel,
	&h2fmi_ewritel,
};

static int s5l8930_h2fmi_init(SysBusDevice *dev)
{
    h2fmi_state_t *h2fmi_state = FROM_SYSBUS(h2fmi_state_t, dev);
    int cregs, nregs, eregs;

	h2fmi_state->ecc_stages = h2fmi_state->page_size >> h2fmi_state->ecc_shift;

    cregs = cpu_register_io_memory(h2fmi_cread, h2fmi_cwrite, h2fmi_state,
			DEVICE_LITTLE_ENDIAN);
    nregs = cpu_register_io_memory(h2fmi_nread, h2fmi_nwrite, h2fmi_state,
			DEVICE_LITTLE_ENDIAN);
    eregs = cpu_register_io_memory(h2fmi_eread, h2fmi_ewrite, h2fmi_state,
			DEVICE_LITTLE_ENDIAN);

    sysbus_init_mmio(dev, 0xff, cregs);
    sysbus_init_mmio(dev, 0xff, nregs);
    sysbus_init_mmio(dev, 0xff, eregs);
    sysbus_init_irq(dev, &h2fmi_state->irq);

	initNandHash();

	if(h2fmi_state->ce_paths)
		h2fmi_setup_chips(h2fmi_state, h2fmi_state->ce_paths);

    return 0;
}

static SysBusDeviceInfo s5l8930_h2fmi_info0 = {
    .init = s5l8930_h2fmi_init,
    .qdev.name = "s5l8930_h2fmi0",
    .qdev.size = sizeof(h2fmi_state_t),
    .qdev.props = (Property[]) {
		DEFINE_PROP_STRING("file", h2fmi_state_t, ce_paths),
		DEFINE_PROP_UINT32("page_size", h2fmi_state_t, page_size, 4096),
		DEFINE_PROP_UINT32("fmtn", h2fmi_state_t, fmtn, 0),
		DEFINE_PROP_UINT32("meta_size", h2fmi_state_t, meta_size, 12),
		DEFINE_PROP_UINT32("ecc_shift", h2fmi_state_t, ecc_shift, 10),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static SysBusDeviceInfo s5l8930_h2fmi_info1 = {
    .init = s5l8930_h2fmi_init,
    .qdev.name = "s5l8930_h2fmi1",
    .qdev.size = sizeof(h2fmi_state_t),
    .qdev.props = (Property[]) {
        DEFINE_PROP_STRING("file", h2fmi_state_t, ce_paths),
        DEFINE_PROP_UINT32("page_size", h2fmi_state_t, page_size, 4096),
		DEFINE_PROP_UINT32("fmtn", h2fmi_state_t, fmtn, 1),
        DEFINE_PROP_UINT32("meta_size", h2fmi_state_t, meta_size, 12),
        DEFINE_PROP_UINT32("ecc_shift", h2fmi_state_t, ecc_shift, 10),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5l8930_h2fmi_register_devices(void)
{
    sysbus_register_withprop(&s5l8930_h2fmi_info0);
	sysbus_register_withprop(&s5l8930_h2fmi_info1);
}

device_init(s5l8930_h2fmi_register_devices);

DeviceState *s5l8930_h2fmi0_register(target_phys_addr_t base, qemu_irq irq)
{
    DeviceState *dev = qdev_create(NULL, "s5l8930_h2fmi0");
    qdev_init_nofail(dev);
	
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base + H2FMI_CBASE);
    sysbus_mmio_map(sysbus_from_qdev(dev), 1, base + H2FMI_NBASE);
    sysbus_mmio_map(sysbus_from_qdev(dev), 2, base + H2FMI_EBASE);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);

    return dev;
}

DeviceState *s5l8930_h2fmi1_register(target_phys_addr_t base, qemu_irq irq)
{
    DeviceState *dev = qdev_create(NULL, "s5l8930_h2fmi1");
    qdev_init_nofail(dev);

    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base + H2FMI_CBASE);
    sysbus_mmio_map(sysbus_from_qdev(dev), 1, base + H2FMI_NBASE);
    sysbus_mmio_map(sysbus_from_qdev(dev), 2, base + H2FMI_EBASE);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);

    return dev;
}

