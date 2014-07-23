/*
 *	Parses EEPROM text file and createds binary .eep file
 *	Usage: eepmake input_file output_file
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#include "eeptypes.h"

#define GPIO_MAX 28
#define HEADER_SIGN 0x522d5069 //"R-Pi" in ASCII


//todo: larger initial mallocs

struct header_t header;
struct atom_t *custom_atom, vinf_atom, gpio_atom, dt_atom;
struct vendor_info_d* vinf;
struct gpio_map_d* gpiomap;

bool product_serial_set, product_id_set, product_ver_set, vendor_set, product_set, 
			gpio_drive_set, gpio_slew_set, gpio_hyteresis_set;
			
bool data_receive, has_dt, receive_dt;
			
char **data;
char *current_atom; //rearranged to write out
unsigned int data_len, custom_ct, total_size, data_cap, custom_cap;


int write_binary(char* out) {
	FILE *fp;
	int i, offset;
	short crc;
	
	fp=fopen(out, "wb");
	if (!fp) {
		printf("Error writing file %s\n", out);
		return -1;
	}
	
	fwrite(&header, sizeof(header), 1, fp);
		
		
	current_atom = (char *) malloc(vinf_atom.dlen+8);
	offset = 0;
	//vendor information atom first part
	memcpy(current_atom, &vinf_atom, 8);
	offset += 8;
	//data first part
	memcpy(current_atom+offset, vinf_atom.data, 10);
	offset += 10;	
	//data strings
	memcpy(current_atom+offset, vinf->vstr, vinf->vslen);
	offset += vinf->vslen;
	memcpy(current_atom+offset, vinf->pstr, vinf->pslen);
	offset += vinf->pslen;
	//vinf last part
	crc = getcrc(current_atom, offset);
	memcpy(current_atom+offset, &crc, 2);
	offset += 2;
	
	fwrite(current_atom, offset, 1, fp);
	free(current_atom);
	
	current_atom = (char *) malloc(gpio_atom.dlen+8);
	offset = 0;
	//GPIO map first part
	memcpy(current_atom, &gpio_atom, 8);
	offset += 8;
	//GPIO data
	memcpy(current_atom+offset, gpiomap, 30);
	offset += 30;
	//GPIO map last part
	crc = getcrc(current_atom, offset);
	memcpy(current_atom+offset, &crc, 2);
	offset += 2;
	
	fwrite(current_atom, offset, 1, fp);
	free(current_atom);
	
	if (has_dt) {
		current_atom = (char *) malloc(dt_atom.dlen+8);
		offset = 0;
		
		memcpy(current_atom, &dt_atom, 8);
		offset += 8;
		
		memcpy(current_atom+offset, dt_atom.data, dt_atom.dlen-2);
		offset += dt_atom.dlen-2;
		
		crc = getcrc(current_atom, offset);
		memcpy(current_atom+offset, &crc, 2);
		offset += 2;
		
		fwrite(current_atom, offset, 1, fp);
		free(current_atom);
	}
	
	for (i = 0; i<custom_ct; i++) {
		current_atom = (char *) malloc(custom_atom[i].dlen+8);
		offset = 0;
		
		memcpy(current_atom, &custom_atom[i], 8);
		offset += 8;
		
		memcpy(current_atom+offset, custom_atom[i].data, custom_atom[i].dlen-2);
		offset += custom_atom[i].dlen-2;
		
		crc = getcrc(current_atom, offset);
		memcpy(current_atom+offset, &crc, 2);
		offset += 2;
		
		fwrite(current_atom, offset, 1, fp);
		free(current_atom);
	}
	
	fflush(fp);
	fclose(fp);
	return 0;
}


void parse_data(char* c) {
	int k;
	char s;
	char* i = c;
	char* j = c;
	while(*j != '\0')
	{
		*i = *j++;
		if(isxdigit(*i))
			i++;
	}
	*i = '\0';
	
	int len = strlen(c);
	if (len % 2 != 0) {
		printf("Error: data must have an even number of hex digits\n");
	} else {
		for (k = 0; k<len/2; k++) {
			//read a byte at a time
			s = *(c+2);
			*(c+2)='\0';
			
			if (data_len==data_cap) {
				data_cap *=2;
				*data = (char *) realloc(*data, data_cap);
			}
			
			sscanf(c, "%2x", *data+data_len++);
			
			*(c+2) = s;
			c+=2;
		}
	}
}


void finish_data() {
	if (data_receive) {
		*data = (char *) realloc(*data, data_len);
			
		total_size+=ATOM_SIZE+data_len;

		if (receive_dt) {
			dt_atom.type = ATOM_DT;
			dt_atom.count = 2;
			dt_atom.dlen = data_len+2;
		} else {
			//finish atom description
			custom_atom[custom_ct].type = ATOM_CUSTOM;
			custom_atom[custom_ct].count = 3+custom_ct;
			custom_atom[custom_ct].dlen = data_len+2;
			
			custom_ct++;
		}
	}
}


void parse_command(char* cmd, char* c) {
	int val;
	char *fn, *pull;
	char pin;
	bool valid;
	bool continue_data=false;
	
	/* Vendor info related part */
	if (strcmp(cmd, "product_serial")==0) {
		product_serial_set = true; //required field
		sscanf(c, "%100s %x\n", cmd, &vinf->serial);
		
	} else if (strcmp(cmd, "product_id")==0) {
		product_id_set = true; //required field
		sscanf(c, "%100s %hx", cmd, &vinf->pid);
		
	} else if (strcmp(cmd, "product_ver")==0) {
		product_ver_set = true; //required field
		sscanf(c, "%100s %hx", cmd, &vinf->pver);
		
	} else if (strcmp(cmd, "vendor")==0) {
		vendor_set = true; //required field
		
		vinf->vstr = (char*) malloc (256);
		sscanf(c, "%100s \"%255[^\"]\"", cmd, vinf->vstr);
		
		total_size-=vinf->vslen;
		vinf_atom.dlen-=vinf->vslen;
		
		vinf->vslen = strlen(vinf->vstr);
		
		total_size+=vinf->vslen;
		vinf_atom.dlen+=vinf->vslen;
		
	} else if (strcmp(cmd, "product")==0) {
		product_set = true; //required field
		
		vinf->pstr = (char*) malloc (256);
		sscanf(c, "%100s \"%255[^\"]\"", cmd, vinf->pstr);
		
		total_size-=vinf->pslen;
		vinf_atom.dlen-=vinf->pslen;
		
		vinf->pslen = strlen(vinf->pstr);
		
		total_size+=vinf->pslen;
		vinf_atom.dlen+=vinf->pslen;
	} 
	
	/* GPIO map related part */
	else if (strcmp(cmd, "gpio_drive")==0) {
		gpio_drive_set = true; //required field
		
		sscanf(c, "%100s %1x", cmd, &val);
		if (val>8 || val<0) printf("Warning: gpio_drive property in invalid region, using default value instead\n");
		else gpiomap->flags |= val;
		
		
	} else if (strcmp(cmd, "gpio_slew")==0) {
		gpio_slew_set = true; //required field
		
		sscanf(c, "%100s %1x", cmd, &val);
		
		if (val>2 || val<0) printf("Warning: gpio_slew property in invalid region, using default value instead\n");
		else gpiomap->flags |= val<<4;
		
	} else if (strcmp(cmd, "gpio_hyteresis")==0) {
		gpio_hyteresis_set = true; //required field
		
		sscanf(c, "%100s %1x", cmd, &val);
		
		if (val>2 || val<0) printf("Warning: gpio_slew property in invalid region, using default value instead\n");
		else gpiomap->flags |= val<<6;
		
	} else if (strcmp(cmd, "setgpio")==0) {
		fn = (char*) malloc (101);
		pull = (char*) malloc (101);
		
		sscanf(c, "%100s %d %100s %100s", cmd, &val, fn, pull);
		
		if (val<2 || val>GPIO_MAX) printf("Error: GPIO number out of bounds\n");
		else {
			valid = true;
			pin = 0;
			
			if (strcmp(fn, "INPUT")==0) {
				//no action
			} else if (strcmp(fn, "OUTPUT")==0) {
				pin |= 1;
			} else if (strcmp(fn, "ALT0")==0) {
				pin |= 4;
			} else if (strcmp(fn, "ALT1")==0) {
				pin |= 5;
			} else if (strcmp(fn, "ALT2")==0) {
				pin |= 6;
			} else if (strcmp(fn, "ALT3")==0) {
				pin |= 7;
			} else if (strcmp(fn, "ALT4")==0) {
				pin |= 3;
			} else if (strcmp(fn, "ALT5")==0) {
				pin |= 2;
			} else {
				printf("Error at setgpio: function type not recognised\n");
				valid=false;
			}
			
			if (strcmp(pull, "DEFAULT")==0) {
				//no action
			} else if (strcmp(pull, "UP")==0) {
				pin |= 1<<5;
			} else if (strcmp(pull, "DOWN")==0) {
				pin |= 2<<5;
			} else if (strcmp(pull, "NONE")==0) {
				pin |= 3<<5;
			} else {
				printf("Error at setgpio: pull type not recognised\n");
				valid=false;
			}
			
			pin |= 1<<7; //board uses this pin
			
			if (valid) gpiomap->pins[val] = pin;
		}
	} 
	
	/* DT atom related part */
	else if (strcmp(cmd, "dt_blob")==0) {
		finish_data();
		
		has_dt = true;
		c+=strlen("dt_blob");
		
		receive_dt=true;
		data_receive=true;
		
		data_len = 0;
		data_cap = 4;
		data = &dt_atom.data;
		*data = (char *) malloc(data_cap);
		
		parse_data(c);
		continue_data = true;
	
	} 
	
	/* Custom data related part */
	else if (strcmp(cmd, "custom_data")==0) {
		finish_data();
		
		c+=strlen("custom_data");
		
		if (custom_cap == custom_ct) {
			custom_cap *= 2;
			custom_atom = (struct atom_t*) realloc(custom_atom, custom_cap * sizeof(struct atom_t));
		}
		
		receive_dt=false;
		data_receive=true;
		
		data_len = 0;
		data_cap = 4;
		data = &custom_atom[custom_ct].data;
		*data = (char *) malloc(data_cap);
		
		parse_data(c);
		continue_data = true;
	
	} else if (strcmp(cmd, "end") ==0) {
		//close last data atom
		continue_data=false;
	}
	/* Incoming data */
	else if (data_receive) {
		parse_data(c);
		continue_data = true;
	} 
	
	
	if (!continue_data) finish_data();
	
}

int read_text(char* in) {
	FILE * fp;
	char * line = NULL;
	char * c = NULL;
	size_t len = 0;
	ssize_t read;
	char *comment = NULL;
	int atomct = 2;
	int linect = 0;
	char * command = (char*) malloc (101);
	int i;
	
	has_dt = false;
	
	printf("Opening file %s for read\n", in);
	
	fp = fopen(in, "r");
	if (fp == NULL) {
		printf("Error opening input file\n");
		return -1;
	}

	//allocating memory and setting up required atoms
	custom_cap = 1;
	custom_atom = (struct atom_t*) malloc(sizeof(struct atom_t) * custom_cap);
	
	total_size=ATOM_SIZE*2+HEADER_SIZE+VENDOR_SIZE+GPIO_SIZE;
	
	vinf_atom.type = ATOM_VENDOR_INFO;
	vinf_atom.count = 0;
	vinf = (struct vendor_info_d *) malloc(sizeof(struct vendor_info_d));
	vinf_atom.data = (char *)vinf;
	vinf_atom.dlen = VENDOR_SIZE + 2;
	
	gpio_atom.type = ATOM_GPIO_MAP;
	gpio_atom.count = 1;
	gpiomap = (struct gpio_map_d *) malloc(sizeof(struct gpio_map_d));
	gpio_atom.data = (char *)gpiomap;
	gpio_atom.dlen = GPIO_SIZE + 2;
	
	while ((read = getline(&line, &len, fp)) != -1) {
		linect++;
		c = line;
		
		for (i=0; i<read; i++) if (c[i]=='#') c[i]='\0';
		
		while (isspace(*c)) ++c;
		
		
		if (*c=='\0' || *c=='\n' || *c=='\r') {
			//empty line, do nothing
		} else if (isalnum (*c)) {
			sscanf(c, "%100s", command);
			
#ifdef DEBUG
			printf("Processing line %u: %s", linect, c);
			if ((*(c+strlen(c)-1))!='\n') printf("\n");
#endif
			
			parse_command(command, c);
		
		
		} else printf("Can't parse line %u: %s", linect, c);
	}
	
	finish_data();
	
	if (!product_serial_set || !product_id_set || !product_ver_set || !vendor_set || !product_set || 
			!gpio_drive_set || !gpio_slew_set || !gpio_hyteresis_set) {
			
		printf("Warning: required fields missing in vendor information or GPIO map, using default values\n");
	}
	
	printf("Done reading\n");
	
	return 0;
}

int main(int argc, char *argv[]) {
	int ret;
	int i;
	
	if (argc<3) {
		printf("Wrong input format.\n"); //todo: display help
		return 0;
	}
	
	
	ret = read_text(argv[1]);
	if (ret) {
		printf("Error reading and parsing input, aborting\n");
		return 0;
	}
	
	header.signature = HEADER_SIGN;
	header.ver = FORMAT_VERSION;
	header.res = 0;
	header.numatoms = 2+has_dt+custom_ct;
	header.eeplen = total_size;
	
	printf("Writing out...\n");
	
	ret = write_binary(argv[2]);
	if (ret) {
		printf("Error writing output\n");
		return 0;
	}
	
	printf("Done.\n");

	return 0;
}