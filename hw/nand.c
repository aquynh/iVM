/*
 * Flash NAND memory emulation.  Based on "16M x 8 Bit NAND Flash
 * Memory" datasheet for the KM29U128AT / K9F2808U0A chips from
 * Samsung Electronic.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#ifndef NAND_IO

# include "hw.h"
# include "flash.h"
# include "blockdev.h"
/* FIXME: Pass block device as an argument.  */

# define NAND_CMD_READ0		0x00
# define NAND_CMD_READ1		0x01
# define NAND_CMD_READ2		0x50
# define NAND_CMD_LPREAD2	0x30
# define NAND_CMD_NOSERIALREAD2	0x35
# define NAND_CMD_RANDOMREAD1	0x05
# define NAND_CMD_RANDOMREAD2	0xe0
# define NAND_CMD_READID	0x90
# define NAND_CMD_RESET		0xff
# define NAND_CMD_PAGEPROGRAM1	0x80
# define NAND_CMD_PAGEPROGRAM2	0x10
# define NAND_CMD_CACHEPROGRAM2	0x15
# define NAND_CMD_BLOCKERASE1	0x60
# define NAND_CMD_BLOCKERASE2	0xd0
# define NAND_CMD_READSTATUS	0x70
# define NAND_CMD_COPYBACKPRG1	0x85

# define NAND_IOSTATUS_ERROR	(1 << 0)
# define NAND_IOSTATUS_PLANE0	(1 << 1)
# define NAND_IOSTATUS_PLANE1	(1 << 2)
# define NAND_IOSTATUS_PLANE2	(1 << 3)
# define NAND_IOSTATUS_PLANE3	(1 << 4)
# define NAND_IOSTATUS_BUSY	(1 << 6)
# define NAND_IOSTATUS_UNPROTCT	(1 << 7)

# define MAX_PAGE		0x100D00
# define PAGE_SECTORS       10

struct NANDFlashState {
    uint8_t manf_id, chip_id;
    uint64_t size;
	uint32_t pages;
	uint32_t soff;
    uint8_t *storage;
    BlockDriverState *bdrv;
    int ce, wp, gnd, rb;

    uint8_t io[MAX_PAGE];
    uint32_t *ioaddr;
    int iolen;
	uint32_t iosize;
	uint32_t metaoff;

    uint32_t cmd;
	uint32_t resCode;
    uint64_t addr;
    int addrlen;
    int status;
    int offset;
    uint8_t bdata[(PAGE_SECTORS + 2) * 0x200];

    void (*blk_write)(NANDFlashState *s);
    void (*blk_erase)(NANDFlashState *s);
    void (*blk_load)(NANDFlashState *s, uint64_t addr, int offset);
};

# define NAND_NO_AUTOINCR	0x00000001
# define NAND_BUSWIDTH_16	0x00000002
# define NAND_NO_PADDING	0x00000004
# define NAND_CACHEPRG		0x00000008
# define NAND_COPYBACK		0x00000010
# define NAND_IS_AND		0x00000020
# define NAND_4PAGE_ARRAY	0x00000040
# define NAND_NO_READRDY	0x00000100
# define NAND_SAMSUNG_LP	(NAND_NO_PADDING | NAND_COPYBACK)

# define NAND_IO

# define SECTOR(addr)       ((addr) >> 9)
# define META_READ_SIZE		0xA
# define META_REAL_SIZE		0xD

# define CE_LENGTH		0x100000
# define PAGE_SIZE      4096
# define PAGE_START(page)   (page * (PAGE_SIZE + META_REAL_SIZE))
# include "nand.c"

static const struct {
    uint64_t size;
    int width;
    uint32_t options;
} nand_flash_ids[0x100] = {
    [0 ... 0xff] = { 0 },

	/* 128 Gigabit */
    [0x2c] = {0, 0, 0},

};

void nand_reset(NANDFlashState *s)
{
    s->cmd = NAND_CMD_READ0;
	s->resCode = 0;
    s->addr = 0;
    s->addrlen = 0;
    s->iolen = 0;
    //s->status &= NAND_IOSTATUS_UNPROTCT;
}

static void nand_command(NANDFlashState *s)
{
#if 0
    unsigned int offset;
    switch (s->cmd) {
    case NAND_CMD_READ0:
        s->iolen = 0;
        break;

    case NAND_CMD_READID:
        s->io[0] = s->manf_id;
        s->io[1] = s->chip_id;
        s->io[2] = 'Q';		/* Don't-care byte (often 0xa5) */
        if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP)
            s->io[3] = 0x15;	/* Page Size, Block Size, Spare Size.. */
        else
            s->io[3] = 0xc0;	/* Multi-plane */
     
		// HACK 
        s->ioaddr = s->io;
        s->iolen = 4;
        break;

    case NAND_CMD_RANDOMREAD2:
    case NAND_CMD_NOSERIALREAD2:
    fprintf(stderr, "%s: random read\n", __FUNCTION__);
        if (!(nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP))
            break;
        offset = s->addr & ((1 << s->addr_shift) - 1);
        s->blk_load(s, s->addr, offset);
        if (s->gnd)
            s->iolen = (1 << s->page_shift) - offset;
        else
            s->iolen = (1 << s->page_shift) + (1 << s->oob_shift) - offset;
        break;

    case NAND_CMD_RESET:
        nand_reset(s);
        break;

    case NAND_CMD_PAGEPROGRAM1:
        s->ioaddr = s->io;
        s->iolen = 0;
        break;

    case NAND_CMD_PAGEPROGRAM2:
        if (s->wp) {
            s->blk_write(s);
        }
        break;

    case NAND_CMD_BLOCKERASE1:
        break;

    case NAND_CMD_BLOCKERASE2:
        if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP)
            s->addr <<= 16;
        else
            s->addr <<= 8;

        if (s->wp) {
            s->blk_erase(s);
        }
        break;

    case NAND_CMD_READSTATUS:
        s->io[0] = s->status | 0x40; // bit7 :ready
        s->ioaddr = s->io;
        s->iolen = 1;
        break;

    default:
        printf("%s: Unknown NAND command 0x%02x\n", __FUNCTION__, s->cmd);
    }
#endif
}

static void nand_save(QEMUFile *f, void *opaque)
{
#if 0
    NANDFlashState *s = (NANDFlashState *) opaque;
    qemu_put_byte(f, s->cle);
    qemu_put_byte(f, s->ale);
    qemu_put_byte(f, s->ce);
    qemu_put_byte(f, s->wp);
    qemu_put_byte(f, s->gnd);
    qemu_put_buffer(f, s->io, sizeof(s->io));
    qemu_put_be32(f, s->ioaddr - s->io);
    qemu_put_be32(f, s->iolen);

    qemu_put_be32s(f, &s->cmd);
    qemu_put_be64s(f, &s->addr);
    qemu_put_be32(f, s->addrlen);
    qemu_put_be32(f, s->status);
    qemu_put_be32(f, s->offset);
    /* XXX: do we want to save s->storage too? */
#endif
}

static int nand_load(QEMUFile *f, void *opaque, int version_id)
{
#if 0
    NANDFlashState *s = (NANDFlashState *) opaque;
    s->cle = qemu_get_byte(f);
    s->ale = qemu_get_byte(f);
    s->ce = qemu_get_byte(f);
    s->wp = qemu_get_byte(f);
    s->gnd = qemu_get_byte(f);
    qemu_get_buffer(f, s->io, sizeof(s->io));
    s->ioaddr = s->io + qemu_get_be32(f);
    s->iolen = qemu_get_be32(f);
    if (s->ioaddr >= s->io + sizeof(s->io) || s->ioaddr < s->io)
        return -EINVAL;

    qemu_get_be32s(f, &s->cmd);
    qemu_get_be64s(f, &s->addr);
    s->addrlen = qemu_get_be32(f);
    s->status = qemu_get_be32(f);
    s->offset = qemu_get_be32(f);
#endif
    return 0;
}

/*
 * Chip inputs are CLE, ALE, CE, WP, GND and eight I/O pins.  Chip
 * outputs are R/B and eight I/O pins.
 *
 * CE, WP and R/B are active low.
 */
void nand_setpins(NANDFlashState *s,
                int cle, int ale, int ce, int wp, int gnd)
{
    s->ce = ce;
}

void nand_getpins(NANDFlashState *s, int *rb)
{
    *rb = s->rb;
    s->rb = 1;
}

void nand_setio(NANDFlashState *s, uint8_t value)
{
    if (!s->ce)
		return;

   	fprintf(stderr, "%s: nand_setio: 0x%x\n", __FUNCTION__, value);
	
	switch(value) {
			case NAND_CMD_READ0: /* Single page read */
				s->cmd = NAND_CMD_READ0;
				break;
	}

#if 0
    if (!s->ce && s->cle) {
        if (value == NAND_CMD_READ0) {
            s->offset = 0;
			fprintf(stderr, "read0\n");
		} else if (value == NAND_CMD_READ1) {
            s->offset = 0x100;
            value = NAND_CMD_READ0;
        } else if (value == NAND_CMD_READ2) {
            s->offset = 1 << s->page_shift;
            value = NAND_CMD_READ0;
        }

        s->cmd = value;

        if (s->cmd == NAND_CMD_READSTATUS ||
                s->cmd == NAND_CMD_PAGEPROGRAM2 ||
                s->cmd == NAND_CMD_BLOCKERASE1 ||
                s->cmd == NAND_CMD_BLOCKERASE2 ||
                s->cmd == NAND_CMD_NOSERIALREAD2 ||
                s->cmd == NAND_CMD_RANDOMREAD2 ||
                s->cmd == NAND_CMD_RESET)
            nand_command(s);

        if (s->cmd != NAND_CMD_RANDOMREAD2) {
            s->addrlen = 0;
        }
    }

    if (!s->ce && s->ale) {
        uint64_t shift = s->addrlen * 8;
        uint64_t mask = ~((uint64_t)0xff << shift);
        uint64_t v = (uint64_t)value << shift;

        s->addr = (s->addr & mask) | v;
        s->addrlen ++;
//fprintf (stderr, "setio: ale: value: %x, addr: %016lX, addrlen: %x v: %016lX, mask: %016lX\n", value, s->addr, s->addrlen, v, mask);

        if (s->addrlen == 1 && s->cmd == NAND_CMD_READID)
            nand_command(s);

        if (!(nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
                s->addrlen == 3 && (
                    s->cmd == NAND_CMD_READ0 ||
                    s->cmd == NAND_CMD_PAGEPROGRAM1))
            nand_command(s);
        if ((nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
               s->addrlen == 4 && (
                    s->cmd == NAND_CMD_READ0 ||
                    s->cmd == NAND_CMD_PAGEPROGRAM1))
            nand_command(s);
    }
#endif
	/*
    if (!s->ce && !s->cle && !s->ale && s->cmd == NAND_CMD_PAGEPROGRAM1) {
        if (s->iolen < (1 << s->page_shift) + (1 << s->oob_shift))
            s->io[s->iolen ++] = value;
    } else if (!s->ce && !s->cle && !s->ale && s->cmd == NAND_CMD_COPYBACKPRG1) {
        if ((s->addr & ((1 << s->addr_shift) - 1)) <
                (1 << s->page_shift) + (1 << s->oob_shift)) {
            s->io[s->iolen + (s->addr & ((1 << s->addr_shift) - 1))] = value;
            s->addr ++;
        }
    }
	*/
}

uint8_t nand_getio(NANDFlashState *s)
{
#if 0
    if (!s->ce){
	// chip not enabled
        return 0;
	}

    if (!s->iolen && s->cmd == NAND_CMD_READ0) {
    	fprintf(stderr, "%s: READ0 load %lx %lx\n", __FUNCTION__, s->addr, PAGE_START(s->addr));
        s->blk_load(s, s->addr);
    }
/*
	if(s->iolen && s->cmd == NAND_CMD_READ_META) {
		s->ioaddr += PAGE_SIZE;
	}
*/
    if (!s->ce || s->iolen <= 0){
	fprintf(stderr, "%s: nand_getio: 0x0 (iolen: 0x%x)\n", __FUNCTION__, s->iolen);
        return 0;
	}
#endif
    return s->resCode;
}

NANDFlashState *nand_init(int manf_id, int chip_id)
{
    int pagesize;
    NANDFlashState *s;
    DriveInfo *dinfo;

/*
    if (nand_flash_ids[chip_id].size == 0) {
        hw_error("%s: Unsupported NAND chip ID.\n", __FUNCTION__);
    }
*/

    s = (NANDFlashState *) qemu_mallocz(sizeof(NANDFlashState));
    dinfo = drive_get_next(IF_MTD);
    if (dinfo) {
        s->bdrv = dinfo->bdrv;
		fprintf(stderr, "%s: setting s->bdrv to %p\n", __FUNCTION__, s->bdrv);
	}
    s->manf_id = manf_id;
    s->chip_id = chip_id;
    //s->size = 1 << 34;//nand_flash_ids[s->chip_id].size;

	nand_init_4096(s);

    register_savevm(NULL, "nand", -1, 0, nand_save, nand_load, s);

    return s;
}

void nand_done(NANDFlashState *s)
{
    if (s->bdrv) {
        bdrv_close(s->bdrv);
        bdrv_delete(s->bdrv);
    }

    qemu_free(s);
}

#else
uint32_t nand_read_meta(NANDFlashState *s)
{
		uint32_t metadata;
		metadata = s->ioaddr[(PAGE_SIZE/4) + s->metaoff];
		s->metaoff++;
		return metadata;
}

uint32_t nand_read_page(NANDFlashState *s, uint32_t ce, uint32_t offset, uint32_t size, uint32_t *res)
{
    if (!ce) {
        fprintf(stderr, "%s: nand_read error with bad ce value\n", __FUNCTION__);
        *res = 0x80000025;
		return 0;
    }

    if (offset >= s->pages) {
        fprintf(stderr, "%s: nand_read error page > num_of_pages\n", __FUNCTION__);
        *res = 0x80000025;
		return 0;
    }

	*res = 0;
    if (s->bdrv) {
			if(s->iolen <= 0) {
					s->soff = (PAGE_START(offset) * ce) - (SECTOR(PAGE_START(offset) * ce) * 512);
					uint64_t roffset = (((4109ull * offset) * ce ) >> 9);
					fprintf(stderr, "%s: nand: read @ addr: 0x%x page_start: 0x%llx soff: 0x%08x ce: %d\n", __FUNCTION__, offset, roffset, s->soff, ce);
					if (bdrv_read(s->bdrv, roffset, s->bdata, PAGE_SECTORS) == -1) {
							printf("%s: read error in sector %llx\n", __FUNCTION__, roffset);
							*res = 0x80000025;
							return 0;
					}
					/* ioaddr points to actual page minus page flag*/
					s->ioaddr = (uint32_t *)(s->bdata+s->soff+1);
					
                    s->offset = 0;
					s->metaoff = 0;
					/* Check page flag */
					if(*(s->bdata+s->soff) == 0x30)
					{
						*res = 0x80000003;
					}
					s->iolen = size - 4;
					return s->ioaddr[s->offset];
			} else {	
                    /* Check page flag */
                    if(*(s->bdata+s->soff) == 0x30)
                    {
                    	*res = 0x80000003;
                    }
					s->iolen -= 4;
					s->offset++;
                    return s->ioaddr[s->offset];
			}
	} else {
			fprintf(stderr, "%s: couldn't find valid block device\n", __FUNCTION__);
    }
	*res = 0x80000025;
    return 0x0;
}

#if 0
uint32_t nand_read(NANDFlashState *s, uint8_t *buffer, uint32_t ce, uint32_t offset, uint32_t size)
{
    uint8_t bdata[(PAGE_SECTORS + 2) * 0x200];

    if (!ce) {
        fprintf(stderr, "%s: nand_read error with bad ce value\n", __FUNCTION__);
        return 0x80000025;
    }

    if (offset >= s->pages) {
        fprintf(stderr, "%s: nand_read error page > num_of_pages\n", __FUNCTION__);
        return 0x80000025;
    }

    if (s->bdrv) {
			uint32_t soff = (PAGE_START(offset) * ce) - (SECTOR(PAGE_START(offset) * ce) * 512);
			uint64_t roffset = (((4109ull * offset) * ce) >> 9);
            fprintf(stderr, "%s: nand: read @ addr: 0x%x page_start: 0x%llx soff: 0x%08x)\n", __FUNCTION__, offset, roffset, soff);
            if (bdrv_read(s->bdrv, roffset, bdata, PAGE_SECTORS) == -1) {
                printf("%s: read error in sector %llx\n", __FUNCTION__, roffset);
                return 0x80000025;
            }

			if(size != META_READ_SIZE) {
            	if(*(bdata+soff) == 0x31)
            	{
                	return 0x80000003;
            	}
				memcpy(buffer, bdata+soff+1, size);
			} else {
				memcpy(buffer, bdata+soff+PAGE_SIZE+1, size);	
			}
    } else {
			fprintf(stderr, "%s: couldn't find valid block device\n", __FUNCTION__);

	}

    return 0x0;
}
#endif
/* Program a single page */
static void glue(nand_blk_write_, PAGE_SIZE)(NANDFlashState *s)
{

#if 0
    uint32_t off, page, sector, soff;
    uint8_t iobuf[(PAGE_SECTORS + 2) * 0x200];
    if (PAGE(s->addr) >= s->pages)
        return;
    hw_error("NAND write not yet supported\n");

    if (!s->bdrv) {
        memcpy(s->storage + PAGE_START(s->addr) + (s->addr & PAGE_MASK) +
                        s->offset, s->io, s->iolen);
    } else if (s->mem_oob) {
        sector = SECTOR(s->addr);
        off = (s->addr & PAGE_MASK) + s->offset;
        soff = SECTOR_OFFSET(s->addr);
        if (bdrv_read(s->bdrv, sector, iobuf, PAGE_SECTORS) == -1) {
            printf("%s: read error in sector %i\n", __FUNCTION__, sector);
            return;
        }

        memcpy(iobuf + (soff | off), s->io, MIN(s->iolen, PAGE_SIZE - off));
        if (off + s->iolen > PAGE_SIZE) {
            page = PAGE(s->addr);
            memcpy(s->storage + (page << OOB_SHIFT), s->io + PAGE_SIZE - off,
                            MIN(OOB_SIZE, off + s->iolen - PAGE_SIZE));
        }

        if (bdrv_write(s->bdrv, sector, iobuf, PAGE_SECTORS) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, sector);
    } else {
        off = PAGE_START(s->addr) + (s->addr & PAGE_MASK) + s->offset;
        sector = off >> 9;
        soff = off & 0x1ff;
        if (bdrv_read(s->bdrv, sector, iobuf, PAGE_SECTORS + 2) == -1) {
            printf("%s: read error in sector %i\n", __FUNCTION__, sector);
            return;
        }

        memcpy(iobuf + soff, s->io, s->iolen);

        if (bdrv_write(s->bdrv, sector, iobuf, PAGE_SECTORS + 2) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, sector);
    }
    s->offset = 0;
#endif
}

/* Erase a single block */
static void glue(nand_blk_erase_, PAGE_SIZE)(NANDFlashState *s)
{
#if 0
    uint32_t i, page;
    uint64_t addr;
    uint8_t iobuf[0x200] = { [0 ... 0x1ff] = 0xff, };
    addr = s->addr & ~((1 << (ADDR_SHIFT + s->erase_shift)) - 1);

    hw_error("NAND write not yet supported\n");
    if (PAGE(addr) >= s->pages)
        return;

    if (!s->bdrv) {
        memset(s->storage + PAGE_START(addr),
                        0xff, (PAGE_SIZE + OOB_SIZE) << s->erase_shift);
    } else if (s->mem_oob) {
        memset(s->storage + (PAGE(addr) << OOB_SHIFT),
                        0xff, OOB_SIZE << s->erase_shift);
        i = SECTOR(addr);
        page = SECTOR(addr + (ADDR_SHIFT + s->erase_shift));
        for (; i < page; i ++)
            if (bdrv_write(s->bdrv, i, iobuf, 1) == -1)
                printf("%s: write error in sector %i\n", __FUNCTION__, i);
    } else {
        addr = PAGE_START(addr);
        page = addr >> 9;
        if (bdrv_read(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: read error in sector %i\n", __FUNCTION__, page);
        memset(iobuf + (addr & 0x1ff), 0xff, (~addr & 0x1ff) + 1);
        if (bdrv_write(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, page);

        memset(iobuf, 0xff, 0x200);
        i = (addr & ~0x1ff) + 0x200;
        for (addr += ((PAGE_SIZE + OOB_SIZE) << s->erase_shift) - 0x200;
                        i < addr; i += 0x200)
            if (bdrv_write(s->bdrv, i >> 9, iobuf, 1) == -1)
                printf("%s: write error in sector %i\n", __FUNCTION__, i >> 9);

        page = i >> 9;
        if (bdrv_read(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: read error in sector %i\n", __FUNCTION__, page);
        memset(iobuf, 0xff, ((addr - 1) & 0x1ff) + 1);
        if (bdrv_write(s->bdrv, page, iobuf, 1) == -1)
            printf("%s: write error in sector %i\n", __FUNCTION__, page);
    }
#endif 
}

/* read a single page */
static void glue(nand_blk_load_, PAGE_SIZE)(NANDFlashState *s,
                uint64_t addr)
{
#if 0
    if (PAGE(addr) >= s->pages)
        return;

    if (s->bdrv) {
		    fprintf(stderr, "%s: nand: read @ addr: 0x%lx page_start: 0x%lx)\n", __FUNCTION__, addr, PAGE_START(addr));
            if (bdrv_read(s->bdrv, PAGE_START(addr) * s->ce, s->io, s->iolen+1) == -1) {
                printf("%s: read error in page %lx\n", __FUNCTION__, PAGE_START(addr) * s->ce);
				s->resCode = 0x80000025;
				s->iolen = 0;
				return;
			}
			// Check if its marked as empty
			if(s->io[0] == 0x31) 
			{
				s->resCode = 0x80000003;
				s->iolen = 0;
				return;
			}
            s->ioaddr = ++s->io; // + PAGE_START(addr);
    }/* else {
	fprintf(stderr, "%s: nand: nodis read @ 0x%lx off: 0x%x)\n", __FUNCTION__, PAGE_START(s->addr), offset);
        memcpy(s->io, s->storage + PAGE_START(s->addr) +
                        offset, PAGE_SIZE + OOB_SIZE - offset);
        s->ioaddr = s->io;
    }
	*/
#endif 
}

static void glue(nand_init_, PAGE_SIZE)(NANDFlashState *s)
{
    s->pages = 0x00100000;

    s->blk_erase = glue(nand_blk_erase_, PAGE_SIZE);
    s->blk_write = glue(nand_blk_write_, PAGE_SIZE);
    s->blk_load = glue(nand_blk_load_, PAGE_SIZE);
}

# undef PAGE_SIZE
# undef PAGE_SHIFT
# undef PAGE_SECTORS
# undef ADDR_SHIFT
#endif	/* NAND_IO */
