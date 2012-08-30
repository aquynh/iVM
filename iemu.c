/*
 * iEmu
 *
 * Written by cmw
 *
 * This code is licenced under the GPL.
 */

#include <stdio.h>
#include "expat.h"
#include <sys/types.h>
#include <dirent.h>

#include "hw/boards.h"
#include "iemu.h"

const static int BUFFER_SIZE = 512;

typedef struct dev_config
{
	char *devname;
	char *skinfile;
	char *bootname;
	char *hwid;
	uint8_t  boottype;
	uint32_t loadaddr;
	char *fwver;
} DevConfig;

static DevConfig *curdev = NULL;
const char *tagval = NULL;
char fwpath[PATH_MAX];
	
#define DEFAULT_DEVICE_PATH 	"devices/"

static int iemu_parse_config(const char *deviceName, char *file);

static inline int value_is(const char *attr, const char *name)
{
    return (strcasecmp(attr, name) == 0);
}

char *iemu_get_skin(void) 
{
	if(curdev)
		return curdev->skinfile;
	return NULL;
}

void iemu_fw_list(const char *device)
{
	DIR *iemufwdir;
	struct dirent *ep;

	sprintf(fwpath, "%s%s", DEFAULT_DEVICE_PATH, device);
	iemufwdir = opendir(fwpath);
	
	if(iemufwdir != NULL)
	{
		printf("Available %s firmware versions\n", device);
		while ((ep = readdir(iemufwdir)) != NULL)
		{
			if((strcmp(ep->d_name, ".") != 0) && (strcmp(ep->d_name, "..") != 0) && ep->d_type == DT_DIR)
			{
				printf("\tVersion: %s\n", ep->d_name);
			}
		}
		closedir(iemufwdir);
	} else {
		fprintf(stderr, "Couldn't open firmware dir %s\n", fwpath);
	}
}

int iemu_fw_load(const char *device, const char *iemuVersion)
{
    char dflist[PATH_MAX];
	struct stat st;

	/* Find default device config */
	sprintf(dflist, "%s%s/config.xml", DEFAULT_DEVICE_PATH, device);
	if(stat(dflist, &st) < 0)
		return 0;

	iemu_parse_config(device, dflist);

	/* Check version */

	return 1;
}

int iemu_fw_init(const char *optarg, const char *device)
{
/*
	if(!iemu_fw_load(device, optarg))
		return -1;
*/
	return 0;
}

static void parser_char_data (void *data, const XML_Char *s, int len)
{
	char *tmp;
	int i, empty;
	DevConfig *parserconfig = (DevConfig*)data;

    assert(tmp = (char *) malloc(len+1));
    strncpy(tmp, s, len);
    tmp[len] = '\0';

   	for(i=0, empty=true; tmp[i]; i++) 
	{
    	if(!isspace(tmp[i]))
		{
			empty = false;
            break;
        }
	}

	if(empty || !tagval) {
		free(tmp);
		return;
	}

	if(value_is(tagval, "device"))
		parserconfig->devname = strdup(tmp);	

	if(value_is(tagval, "hwid"))
		parserconfig->hwid = strdup(tmp);	

    if(value_is(tagval, "skin")) {
		sprintf(fwpath, "%s%s/%s", DEFAULT_DEVICE_PATH, parserconfig->devname, tmp);
        parserconfig->skinfile = strdup(fwpath);
    }

    if(value_is(tagval, "boot")) {
        parserconfig->bootname = strdup(tmp);
		if(value_is(tmp, "iboot")) {
			parserconfig->boottype = BOOT_TYPE_IBOOT;
		}
		if(value_is(tmp, "openiboot")) {
			parserconfig->boottype = BOOT_TYPE_OPENIBOOT;
		}
		if(value_is(tmp, "vrom")) {
			parserconfig->boottype = BOOT_TYPE_VROM;
		}
	}

	if(value_is(tagval, "loadaddr"))
		sscanf(tmp, "0x%08x", &parserconfig->loadaddr);

	free(tmp);
    tagval = NULL;
}

static void parser_start_hndl(void *data, const char *element, const char **attr)
{
	// make sure device is first tag
	tagval = element;
}

static void parser_end_hndl(void *data, const char *element)
{
	// if device but no data error
}

static int iemu_parse_config(const char *deviceName, char *file)
{
    FILE* device_xml;
    char buffer[BUFFER_SIZE];
    int done;
    XML_Parser parser = XML_ParserCreate(NULL);

    assert(curdev = (DevConfig *) malloc(sizeof(DevConfig)));

    if (parser) {
		curdev->devname = strdup(deviceName);

        XML_SetUserData(parser, (void*)curdev);
		XML_SetCharacterDataHandler(parser, parser_char_data); 
        XML_SetElementHandler(parser, parser_start_hndl, parser_end_hndl);

        device_xml = fopen(file, "r");
        printf("device_xml: iemu_config_file = '%s'\n", file);
        if (!device_xml) {
            fprintf(stderr, "Error opening device config file '%s'\n", file);
            XML_ParserFree(parser);
            return -1;
        }

        do {
            if (fgets(buffer, sizeof(buffer), device_xml) == NULL)
                done = 1;
            else {
                done = feof(device_xml);
                if (!XML_Parse(parser, buffer, strlen(buffer), done)) {
                    fprintf(stderr, "Parse error at line %d: %s\n",
                        (int)XML_GetCurrentLineNumber(parser),
                        XML_ErrorString(XML_GetErrorCode(parser)));
                    // Continue anyway
                }
            }
        } while (!done);

        if (done && !feof(device_xml)) {
            fprintf(stderr, "Parse error, unexpected EOF\n");
            // Continue anyway
        }

        XML_ParserFree(parser);

        fclose(device_xml);
		printf("bootname = %s, loadaddr = 0x%08x\n", curdev->bootname, curdev->loadaddr);
        return 0;
    }
    return -1;
}
