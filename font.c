#include <tamtypes.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <malloc.h>

// copied from https://github.com/ps2dev/gsKit/blob/master/ee/toolkit/src/gsToolkit.c
// apparently missing from the linked library, but present in gsToolkit.h
GSFONT *gsKit_init_font_raw(u8 type, u8 *data, int size)
{

	GSFONT *gsFont = calloc(1,sizeof(GSFONT));
	gsFont->Texture = calloc(1,sizeof(GSTEXTURE));
	gsFont->RawData = data;
	gsFont->RawSize = size;
	gsFont->Type = type;
    gsFont->Additional=(short*)malloc(sizeof(short)*256);

	return gsFont;
}

// changes compared to the original gsKit_font_print_scaled:
// - drop PNG logic
// - allow separate scaling factors for X and Y axes
// - adjust scaling for unscii-8 (drop additional char spacing, replace hardcoded 16 with actual font height)
void custom_gsKit_font_print_scaled(GSGLOBAL *gsGlobal, GSFONT *gsFont, float X, float Y, int Z,
                                    float scaleX, float scaleY, unsigned long color, const char *String)
{
	u64 oldalpha;
	u8 oldpabe;
	float cx, cy;
	int i, l;

	oldpabe = gsGlobal->PABE;
	oldalpha = gsGlobal->PrimAlpha;

	gsKit_set_primalpha(gsGlobal, ALPHA_BLEND_ADD, 1);

	cx = X;
	cy = Y;

	l = strlen(String);
	for(i=0; i<l; i++)
	{
		unsigned char c = String[i];
		if(c == '\n')
		{
			cx = X;
			cy += gsFont->CharHeight * scaleY;
		}
		else
		{
			int px, py, charsiz;

			px = c % 16;
			py = (c - px) / 16;
			charsiz = gsFont->Additional[(u8)c];

			gsKit_prim_sprite_texture(gsGlobal, gsFont->Texture, cx, cy,
				(px * gsFont->CharWidth), (py * gsFont->CharHeight),
				cx + (charsiz * scaleX), cy + (gsFont->CharHeight * scaleY),
				(px * gsFont->CharWidth) + charsiz, (py * gsFont->CharHeight) + gsFont->CharHeight,
				Z, color);
			cx += charsiz *scaleX;
		}
	}

	gsKit_set_primalpha(gsGlobal, oldalpha, oldpabe);
}