/*-
 * Copyright (c) 1991-1997 S�ren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <sys/fbio.h>
#include "vgl.h"

static VGLText		*VGLTextFont = 0;

extern byte VGLFont[];

int
VGLTextSetFontFile(char *filename)
{
FILE *fd;

  if (VGLTextFont) {
    if (VGLTextFont->BitmapArray != VGLFont) 
      free (VGLTextFont->BitmapArray);
    free(VGLTextFont);
  }

  if ((VGLTextFont=(VGLText*)malloc(sizeof(VGLText))) == (VGLText*)0)
	return 1;

  if (filename==NULL) {
    VGLTextFont->Width = 8;
    VGLTextFont->Height = 8;
    VGLTextFont->BitmapArray = VGLFont;
  }
  else {
    if ((fd=fopen(filename, "r"))==(FILE*)0)
      return 1;
    fread(&VGLTextFont->Width, 1 , 1, fd);
    fread(&VGLTextFont->Height, 1 , 1, fd);
    VGLTextFont->BitmapArray = 
      (byte*)malloc(256*((VGLTextFont->Width + 7)/8)*VGLTextFont->Height);
    fread(VGLTextFont->BitmapArray, 1, 
      (256*VGLTextFont->Width* VGLTextFont->Height), fd);
    fclose(fd);
  }
  return 0;
}

void
VGLBitmapPutChar(VGLBitmap *Object, int x, int y, byte ch, 
		 byte fgcol, byte bgcol, int fill, int dir)
{
  int lin, bit;

  for(lin = 0; lin < VGLTextFont->Height; lin++) {
    for(bit = 0; bit < VGLTextFont->Width; bit++) {
      if (VGLTextFont->BitmapArray[((ch*VGLTextFont->Height)+lin)]&(1<<bit))
        switch (dir) {
          case 0:
            VGLSetXY(Object, (x+7-bit), (y+lin), fgcol);
	    break;
          case 1:
            VGLSetXY(Object, (x+lin), (y-7+bit), fgcol);
	    break;
          case 2:
            VGLSetXY(Object, (x-7+bit), (y-lin), fgcol);
	    break;
          case 3:
            VGLSetXY(Object, (x-lin), (y+7-bit), fgcol);
	    break;
          case 4:
            VGLSetXY(Object, (x+lin+7-bit), (y+lin+bit), fgcol);
	    break;
        }
      else if (fill)
        switch (dir) {
          case 0:
            VGLSetXY(Object, (x+7-bit), (y+lin), bgcol);
	    break;
          case 1:
            VGLSetXY(Object, (x+lin), (y-7+bit), bgcol);
	    break;
          case 2:
            VGLSetXY(Object, (x-7+bit), (y-lin), bgcol);
	    break;
          case 3:
            VGLSetXY(Object, (x-lin), (y+7-bit), bgcol);
	    break;
          case 4:
            VGLSetXY(Object, (x+lin+7-bit), (y+lin+bit), bgcol);
	    break;
        }
    }
  }
}

void
VGLBitmapString(VGLBitmap *Object, int x, int y, char *str, 
		byte fgcol, byte bgcol, int fill, int dir)
{
  int pos;

  for (pos=0; pos<strlen(str); pos++) {
    switch (dir) {
      case 0:
        VGLBitmapPutChar(Object, x+(pos*VGLTextFont->Width), y, 
                         str[pos], fgcol, bgcol, fill, dir);
	break;
      case 1:
        VGLBitmapPutChar(Object, x, y-(pos*VGLTextFont->Width), 
		         str[pos], fgcol, bgcol, fill, dir);
	break;
      case 2:
        VGLBitmapPutChar(Object, x-(pos*VGLTextFont->Width), y, 
		         str[pos], fgcol, bgcol, fill, dir);
	break;
      case 3:
        VGLBitmapPutChar(Object, x, y+(pos*VGLTextFont->Width),  
		         str[pos], fgcol, bgcol, fill, dir);
	break;
      case 4:
        VGLBitmapPutChar(Object, x+(pos*VGLTextFont->Width),
                         y-(pos*VGLTextFont->Width), 
		         str[pos], fgcol, bgcol, fill, dir);
	break;
    }
  }
}

byte VGLFont[] = {
0,0,0,0,0,0,0,0,126,129,165,129,189,153,129,126,126,255,219,255,195,231,
255,126,108,254,254,254,124,56,16,0,16,56,124,254,124,56,16,0,56,124,56,
254,254,124,56,124,16,16,56,124,254,124,56,124,0,0,24,60,60,24,0,0,255,
255,231,195,195,231,255,255,0,60,102,66,66,102,60,0,255,195,153,189,189,
153,195,255,15,7,15,125,204,204,204,120,60,102,102,102,60,24,126,24,63,
51,63,48,48,112,240,224,127,99,127,99,99,103,230,192,153,90,60,231,231,
60,90,153,128,224,248,254,248,224,128,0,2,14,62,254,62,14,2,0,24,60,126,
24,24,126,60,24,102,102,102,102,102,0,102,0,127,219,219,123,27,27,27,0,
62,99,56,108,108,56,204,120,0,0,0,0,126,126,126,0,24,60,126,24,126,60,24,
255,24,60,126,24,24,24,24,0,24,24,24,24,126,60,24,0,0,24,12,254,12,24,0,
0,0,48,96,254,96,48,0,0,0,0,192,192,192,254,0,0,0,36,102,255,102,36,0,0,
0,24,60,126,255,255,0,0,0,255,255,126,60,24,0,0,0,0,0,0,0,0,0,0,48,120,
120,48,48,0,48,0,108,108,108,0,0,0,0,0,108,108,254,108,254,108,108,0,48,
124,192,120,12,248,48,0,0,198,204,24,48,102,198,0,56,108,56,118,220,204,
118,0,96,96,192,0,0,0,0,0,24,48,96,96,96,48,24,0,96,48,24,24,24,48,96,0,
0,102,60,255,60,102,0,0,0,48,48,252,48,48,0,0,0,0,0,0,0,48,48,96,0,0,0,
252,0,0,0,0,0,0,0,0,0,48,48,0,6,12,24,48,96,192,128,0,124,198,206,222,246,
230,124,0,48,112,48,48,48,48,252,0,120,204,12,56,96,204,252,0,120,204,12,
56,12,204,120,0,28,60,108,204,254,12,30,0,252,192,248,12,12,204,120,0,56,
96,192,248,204,204,120,0,252,204,12,24,48,48,48,0,120,204,204,120,204,204,
120,0,120,204,204,124,12,24,112,0,0,48,48,0,0,48,48,0,0,48,48,0,0,48,48,
96,24,48,96,192,96,48,24,0,0,0,252,0,0,252,0,0,96,48,24,12,24,48,96,0,120,
204,12,24,48,0,48,0,124,198,222,222,222,192,120,0,48,120,204,204,252,204,
204,0,252,102,102,124,102,102,252,0,60,102,192,192,192,102,60,0,248,108,
102,102,102,108,248,0,254,98,104,120,104,98,254,0,254,98,104,120,104,96,
240,0,60,102,192,192,206,102,62,0,204,204,204,252,204,204,204,0,120,48,
48,48,48,48,120,0,30,12,12,12,204,204,120,0,230,102,108,120,108,102,230,
0,240,96,96,96,98,102,254,0,198,238,254,254,214,198,198,0,198,230,246,222,
206,198,198,0,56,108,198,198,198,108,56,0,252,102,102,124,96,96,240,0,120,
204,204,204,220,120,28,0,252,102,102,124,108,102,230,0,120,204,224,112,
28,204,120,0,252,180,48,48,48,48,120,0,204,204,204,204,204,204,252,0,204,
204,204,204,204,120,48,0,198,198,198,214,254,238,198,0,198,198,108,56,56,
108,198,0,204,204,204,120,48,48,120,0,254,198,140,24,50,102,254,0,120,96,
96,96,96,96,120,0,192,96,48,24,12,6,2,0,120,24,24,24,24,24,120,0,16,56,
108,198,0,0,0,0,0,0,0,0,0,0,0,255,48,48,24,0,0,0,0,0,0,0,120,12,124,204,
118,0,224,96,96,124,102,102,220,0,0,0,120,204,192,204,120,0,28,12,12,124,
204,204,118,0,0,0,120,204,252,192,120,0,56,108,96,240,96,96,240,0,0,0,118,
204,204,124,12,248,224,96,108,118,102,102,230,0,48,0,112,48,48,48,120,0,
12,0,12,12,12,204,204,120,224,96,102,108,120,108,230,0,112,48,48,48,48,
48,120,0,0,0,204,254,254,214,198,0,0,0,248,204,204,204,204,0,0,0,120,204,
204,204,120,0,0,0,220,102,102,124,96,240,0,0,118,204,204,124,12,30,0,0,
220,118,102,96,240,0,0,0,124,192,120,12,248,0,16,48,124,48,48,52,24,0,0,
0,204,204,204,204,118,0,0,0,204,204,204,120,48,0,0,0,198,214,254,254,108,
0,0,0,198,108,56,108,198,0,0,0,204,204,204,124,12,248,0,0,252,152,48,100,
252,0,28,48,48,224,48,48,28,0,24,24,24,0,24,24,24,0,224,48,48,28,48,48,
224,0,118,220,0,0,0,0,0,0,0,16,56,108,198,198,254,0,0,0,0,0,0,0,0,0,0,0,
60,126,255,126,24,0,170,85,85,170,170,85,85,170,68,68,68,68,31,4,4,4,124,
64,64,64,31,16,16,16,56,68,68,56,30,17,20,19,64,64,64,124,31,16,16,16,56,
108,56,0,0,0,0,0,0,0,24,24,24,24,126,0,68,100,76,68,16,16,16,31,68,68,40,
16,31,4,4,4,24,24,24,24,248,0,0,0,0,0,0,0,248,24,24,24,0,0,0,0,31,24,24,
24,24,24,24,24,31,0,0,0,24,24,24,24,255,24,24,24,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,255,0,0,0,0,0,0,0,0,0,255,0,0,0,0,0,0,0,0,0,24,24,24,
24,31,24,24,24,24,24,24,24,248,24,24,24,24,24,24,24,255,0,0,0,0,0,0,0,255,
24,24,24,24,24,24,24,24,24,24,24,0,12,48,96,24,12,126,0,0,48,12,6,24,48,
126,0,0,0,3,62,54,54,108,0,0,0,4,126,16,126,64,0,0,28,48,48,48,48,126,0,
0,0,0,24,0,0,0,0,0,0,0,0,0,0,0,0,48,0,48,48,120,120,48,0,0,0,16,124,192,
192,124,16,0,56,96,96,240,96,252,0,0,195,60,102,102,60,195,0,0,204,204,
120,48,252,48,0,24,24,24,0,24,24,24,0,126,192,124,198,124,6,252,0,198,0,
0,0,0,0,0,0,124,130,186,162,186,130,124,0,28,6,30,34,31,63,0,0,0,51,102,
204,102,51,0,0,0,254,6,0,0,0,0,0,0,0,0,0,0,0,0,0,124,130,186,178,170,130,
124,0,254,0,0,0,0,0,0,0,56,108,56,0,0,0,0,0,0,16,124,16,0,124,0,0,28,54,
6,24,62,0,0,0,30,2,14,2,30,0,0,0,24,48,0,0,0,0,0,0,0,0,204,204,204,204,
118,192,126,202,202,126,10,10,10,0,0,0,0,24,0,0,0,0,0,0,0,0,0,0,24,48,6,
14,6,6,6,0,0,0,14,17,17,17,14,31,0,0,0,204,102,51,102,204,0,0,96,224,102,
108,51,103,15,3,96,224,102,108,54,106,4,14,240,32,150,108,51,103,15,3,48,
0,48,96,192,204,120,0,24,12,48,120,204,252,204,0,96,192,48,120,204,252,
204,0,120,132,48,120,204,252,204,0,102,152,48,120,204,252,204,0,204,0,48,
120,204,252,204,0,48,72,48,120,204,252,204,0,62,120,152,156,248,152,158,
0,60,102,192,192,192,102,28,48,48,24,254,98,120,98,254,0,24,48,254,98,120,
98,254,0,56,68,254,98,120,98,254,0,102,0,254,98,120,98,254,0,96,48,120,
48,48,48,120,0,24,48,120,48,48,48,120,0,120,132,120,48,48,48,120,0,204,
0,120,48,48,48,120,0,120,108,102,246,102,108,120,0,102,152,230,246,222,
206,198,0,48,24,124,198,198,198,124,0,24,48,124,198,198,198,124,0,56,68,
124,198,198,198,124,0,102,152,124,198,198,198,124,0,198,0,124,198,198,198,
124,0,0,198,108,56,56,108,198,0,6,124,206,154,178,230,120,192,96,48,204,
204,204,204,252,0,24,48,204,204,204,204,252,0,120,132,204,204,204,204,252,
0,204,0,204,204,204,204,252,0,24,48,204,204,120,48,120,0,96,120,108,120,
96,96,96,0,120,204,196,220,198,198,220,192,48,24,120,12,124,204,118,0,24,
48,120,12,124,204,118,0,120,132,120,12,124,204,118,0,102,152,120,12,124,
204,118,0,204,0,120,12,124,204,118,0,48,72,56,12,124,204,118,0,0,0,236,
50,126,176,110,0,0,0,60,102,192,102,28,48,48,24,120,204,252,192,120,0,24,
48,120,204,252,192,120,0,120,132,120,204,252,192,120,0,204,0,120,204,252,
192,120,0,96,48,0,112,48,48,120,0,24,48,0,112,48,48,120,0,112,136,0,112,
48,48,120,0,204,0,0,112,48,48,120,0,108,56,108,12,108,204,120,0,102,152,
248,204,204,204,204,0,96,48,0,124,198,198,124,0,24,48,0,124,198,198,124,
0,56,68,0,124,198,198,124,0,102,152,0,124,198,198,124,0,198,0,0,124,198,
198,124,0,0,0,24,0,126,0,24,0,0,0,6,124,222,246,124,192,96,48,0,204,204,
204,118,0,24,48,0,204,204,204,118,0,48,72,0,204,204,204,118,0,204,0,0,204,
204,204,118,0,24,48,204,204,204,124,12,248,224,120,108,102,108,120,224,
0,204,0,204,204,204,124,12,248
};
