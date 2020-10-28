/*
 * hex_file_reader.h
 * Parses Intel Hex files and stores the data in a buffer. 
 * 
 * Modified from fileio.c from the avrdude project in order to make it work 
 * within the Arduino framework for an ESP32 cpu. 
 * 
 * Original copyright follows:
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <Arduino.h>

#define IHEX_MAXDATA 256

#define MAX_LINE_LEN 256  /* max line length for ASCII format input files */

struct ihexrec {
  unsigned char    reclen;
  unsigned int     loadofs;
  unsigned char    rectyp;
  unsigned char    data[IHEX_MAXDATA];
  unsigned char    cksum;
};

static uint16_t ihex2b(File * inf,
             uint8_t * memory, uint16_t bufsize, uint32_t fileoffset);

static int ihex_readrec(struct ihexrec * ihex, char * rec);

// Simple redefinition of fgets to work with a C++ File* object.
// This allows the code from avrdude to work with Arduino File* objects.
static char *fgets(char *buffer, uint16_t max_read_length, File *infile) {
  uint16_t i;
  uint8_t first_read = 1;
  for(i=0; i<max_read_length-1; i++) {
    uint16_t numread = infile->readBytes(buffer+i, 1);
    if (numread) {
      if (*(buffer+i) == '\n') {
        return buffer;
      }
    }
    else {
      if (first_read) {
        return NULL;
      }
    }
    first_read = 0;
  }
  return buffer;
}

// ihex_readrec from avrdude
static int ihex_readrec(struct ihexrec * ihex, char * rec)
{
  int i, j;
  char buf[8];
  int offset, len;
  char * e;
  unsigned char cksum;
  int rc;

  len    = strlen(rec);
  offset = 1;
  cksum  = 0;

  /* reclen */
  if (offset + 2 > len)
    return -1;
  for (i=0; i<2; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->reclen = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  /* load offset */
  if (offset + 4 > len)
    return -1;
  for (i=0; i<4; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->loadofs = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  /* record type */
  if (offset + 2 > len)
    return -1;
  for (i=0; i<2; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->rectyp = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  cksum = ihex->reclen + ((ihex->loadofs >> 8) & 0x0ff) + 
    (ihex->loadofs & 0x0ff) + ihex->rectyp;

  /* data */
  for (j=0; j<ihex->reclen; j++) {
    if (offset + 2 > len)
      return -1;
    for (i=0; i<2; i++)
      buf[i] = rec[offset++];
    buf[i] = 0;
    ihex->data[j] = strtoul(buf, &e, 16);
    if (e == buf || *e != 0)
      return -1;
    cksum += ihex->data[j];
  }

  /* cksum */
  if (offset + 2 > len)
    return -1;
  for (i=0; i<2; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->cksum = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  rc = -cksum & 0x000000ff;

  return rc;
}


/*
 * Intel Hex to binary buffer
 *
 * Given an open file 'inf' which contains Intel Hex formated data,
 * parse the file and lay it out within the memory buffer pointed to
 * by outbuf.  The size of outbuf, 'bufsize' is honored; if data would
 * fall outsize of the memory buffer outbuf, an error is generated.
 *
 * Return the maximum memory address within 'outbuf' that was written.
 * If an error occurs, return -1.
 *
 * */

static uint16_t ihex2b(File * inf,
             uint8_t * memory, uint16_t memsize, uint32_t fileoffset)
{
  char buffer [ MAX_LINE_LEN ];
  unsigned int nextaddr, baseaddr, maxaddr;
  int i;
  uint16_t lineno;
  uint8_t len;
  struct ihexrec ihex;
  int rc;

  lineno   = 0;
  baseaddr = 0;
  maxaddr  = 0;
  nextaddr = 0;

  while (fgets((char *)buffer,MAX_LINE_LEN,inf)!=NULL) {
    lineno++;
    ihex.rectyp = -1;
    len = strlen(buffer);
    if (buffer[len-1] == '\n') 
      buffer[--len] = 0;
    if (buffer[0] != ':')
      continue;
    rc = ihex_readrec(&ihex, buffer);
    if (rc < 0) {
      Serial.print("Invalid record at line ");
      Serial.print(lineno);
      Serial.println(" of hex file");

      return -1;
    }
    else if (rc != ihex.cksum) {
      Serial.print("ERROR: checksum mismatch at line ");
      Serial.print(lineno);
      Serial.println(" of file");
      Serial.print("checksum=");
      Serial.print(ihex.cksum);
      Serial.print(", computed checksum=");
      Serial.println(rc);
      return -1;
    }

    switch (ihex.rectyp) {
      case 0: /* data record */
        if (fileoffset != 0 && baseaddr < fileoffset) {
          Serial.print("ERROR: address ");
          Serial.print(baseaddr);
          Serial.print(" out of range (below fileoffset ");
          Serial.print(fileoffset);
          Serial.print(" at line ");
          Serial.print(lineno);
          Serial.println(" of hex file");
          return -1;
        }
        nextaddr = ihex.loadofs + baseaddr - fileoffset;
        if (nextaddr + ihex.reclen > memsize) {
          Serial.print("ERROR: address ");
          Serial.print(nextaddr+ihex.reclen);
          Serial.print(" out of range at line ");
          Serial.print(lineno);
          Serial.println(" of hex file");
          return -1;
        }
        for (i=0; i<ihex.reclen; i++) {
          memory[nextaddr+i] = ihex.data[i];
        }
        if (nextaddr+ihex.reclen > maxaddr)
          maxaddr = nextaddr+ihex.reclen;
        break;

      case 1: /* end of file record */
        return maxaddr;
        break;

      case 2: /* extended segment address record */
        baseaddr = (ihex.data[0] << 8 | ihex.data[1]) << 4;
        break;

      case 3: /* start segment address record */
        /* we don't do anything with the start address */
        break;

      case 4: /* extended linear address record */
        baseaddr = (ihex.data[0] << 8 | ihex.data[1]) << 16;
        break;

      case 5: /* start linear address record */
        /* we don't do anything with the start address */
        break;

      default:
        Serial.print("Don't know how to deal with rectype=");
        Serial.print(ihex.rectyp);
        Serial.print(" at line ");
        Serial.print(lineno);
        Serial.println(" of hex file");
        return -1;
        break;
    }

  } /* while */

  if (maxaddr == 0) {
    Serial.println("ERROR: No valid record found in Intel Hex file");
    return -1;
  }
  else {
    Serial.println("WARNING: no end of file record found in Intel Hex file");
    return maxaddr;
  }
}
