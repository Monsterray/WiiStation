#ifndef DITHER5_H
#define DITHER5_H

// dith5[coeff][v]: the 8-bit channel v dithered to 5 bits -- v>>3, plus one when v&7 is
// above the coefficient and v>>3 is below 31. A table instead of three unpredictable
// branches per pixel; tests/dither_test.c checks it against that rule.
#define DITH5(c,v)    (((v)>>3) + ((((v)&7) > (c)) && ((v) < 0xF8)))
#define DITH5_4(c,v)  DITH5(c,v),DITH5(c,(v)+1),DITH5(c,(v)+2),DITH5(c,(v)+3)
#define DITH5_16(c,v) DITH5_4(c,v),DITH5_4(c,(v)+4),DITH5_4(c,(v)+8),DITH5_4(c,(v)+12)
#define DITH5_64(c,v) DITH5_16(c,v),DITH5_16(c,(v)+16),DITH5_16(c,(v)+32),DITH5_16(c,(v)+48)
#define DITH5_256(c)  { DITH5_64(c,0),DITH5_64(c,64),DITH5_64(c,128),DITH5_64(c,192) }
static const unsigned char dith5[8][256] =
{
 DITH5_256(0), DITH5_256(1), DITH5_256(2), DITH5_256(3),
 DITH5_256(4), DITH5_256(5), DITH5_256(6), DITH5_256(7)
};

#endif
