/**
 * WiiSX - GuiResources.cpp
 * Copyright (C) 2009, 2010 sepp256
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#include "GuiResources.h"
#include "Image.h"
#include "resources.h"
#include "../menu/MenuResources.h"

namespace menu {

Resources::Resources()
{
	defaultButtonImage = new Image(ButtonTexture, 16, 16, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	defaultButtonFocusImage = new Image(ButtonFocusTexture, 16, 16, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
//	styleAButtonImage = new Image(StyleAButtonTexture, 8, 56, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, StyleAButtonTlut, GX_TL_RGB5A3, GX_TLUT0, GX_TLUT_256);
//	styleAButtonFocusImage = new Image(StyleAButtonFocusTexture, 8, 56, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, StyleAButtonTlut, GX_TL_RGB5A3, GX_TLUT0, GX_TLUT_256);
//	styleAButtonSelectOffImage = new Image(StyleAButtonSelectOffTexture, 8, 56, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, StyleAButtonTlut, GX_TL_RGB5A3, GX_TLUT0, GX_TLUT_256);
//	styleAButtonSelectOffFocusImage = new Image(StyleAButtonSelectOffFocusTexture, 8, 56, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, StyleAButtonTlut, GX_TL_RGB5A3, GX_TLUT0, GX_TLUT_256);
//	styleAButtonSelectOnImage = new Image(StyleAButtonSelectOnTexture, 8, 56, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, StyleAButtonTlut, GX_TL_RGB5A3, GX_TLUT0, GX_TLUT_256);
//	styleAButtonSelectOnFocusImage = new Image(StyleAButtonSelectOnFocusTexture, 8, 56, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, StyleAButtonTlut, GX_TL_RGB5A3, GX_TLUT0, GX_TLUT_256);
	styleAButtonImage = new Image(StyleAButtonTexture, 8, 56, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	styleAButtonFocusImage = new Image(StyleAButtonFocusTexture, 8, 56, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	styleAButtonSelectOffImage = new Image(StyleAButtonSelectOffTexture, 8, 56, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	styleAButtonSelectOffFocusImage = new Image(StyleAButtonSelectOffFocusTexture, 8, 56, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	styleAButtonSelectOnImage = new Image(StyleAButtonSelectOnTexture, 8, 56, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	styleAButtonSelectOnFocusImage = new Image(StyleAButtonSelectOnFocusTexture, 8, 56, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
//	menuBackgroundImage = new Image(BackgroundTexture, 848, 480, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	menuBackgroundImage = new Image(BackgroundTexture, 424, 240, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
#ifdef HW_RVL
	logoImage = new Image(LogoTexture, 136, 52, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
#else
	logoImage = new Image(LogoTexture, 176, 52, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
#endif
	controllerEmptyImage = new Image(ControlEmptyTexture, 48, 64, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	controllerGamecubeImage = new Image(ControlGamecubeTexture, 48, 64, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	controllerHidGamecubeImage = new Image(ControlHidGamecubeTexture, 48, 64, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	controllerClassicImage = new Image(ControlClassicTexture, 48, 64, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	controllerWiimoteNunchuckImage = new Image(ControlWiimoteNunchuckTexture, 48, 64, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	controllerWiimoteImage = new Image(ControlWiimoteTexture, 48, 64, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	psxControllerImage = new Image(PsxControllerTexture, 232, 152, GX_TF_IA4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	/* GCon.tx, Just.tx and PSMouse.tx are whole TPL files (magic 00 20 af 30): one IA4 image
	 * whose data starts at 0x40, and whose size is in the header (252x180, 144x256). */
	GConImage = new Image(GConTexture + 0x40, 252, 180, GX_TF_IA4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	JustImage = new Image(JustTexture + 0x40, 252, 180, GX_TF_IA4, GX_CLAMP, GX_CLAMP, GX_FALSE);
	PSMouseImage = new Image(PSMouseTexture + 0x40, 144, 256, GX_TF_IA4, GX_CLAMP, GX_CLAMP, GX_FALSE);

}

Resources::~Resources()
{
	delete defaultButtonImage;
	delete defaultButtonFocusImage;
	delete styleAButtonImage;
	delete styleAButtonFocusImage;
	delete styleAButtonSelectOffImage;
	delete styleAButtonSelectOffFocusImage;
	delete styleAButtonSelectOnImage;
	delete styleAButtonSelectOnFocusImage;
	delete menuBackgroundImage;
	delete logoImage;
	delete controllerEmptyImage;
	delete controllerGamecubeImage;
	delete controllerHidGamecubeImage;
	delete controllerClassicImage;
	delete controllerWiimoteNunchuckImage;
	delete controllerWiimoteImage;
	delete psxControllerImage;
	delete GConImage;
	delete JustImage;
	delete PSMouseImage;
}

Image* Resources::getImage(int image)
{
	Image* returnImage = NULL;
	switch (image)
	{
	case IMAGE_DEFAULT_BUTTON:
		returnImage = defaultButtonImage;
		break;
	case IMAGE_DEFAULT_BUTTONFOCUS:
		returnImage = defaultButtonFocusImage;
		break;
	case IMAGE_STYLEA_BUTTON:
		returnImage = styleAButtonImage;
		break;
	case IMAGE_STYLEA_BUTTONFOCUS:
		returnImage = styleAButtonFocusImage;
		break;
	case IMAGE_STYLEA_BUTTONSELECTOFF:
		returnImage = styleAButtonSelectOffImage;
		break;
	case IMAGE_STYLEA_BUTTONSELECTOFFFOCUS:
		returnImage = styleAButtonSelectOffFocusImage;
		break;
	case IMAGE_STYLEA_BUTTONSELECTON:
		returnImage = styleAButtonSelectOnImage;
		break;
	case IMAGE_STYLEA_BUTTONSELECTONFOCUS:
		returnImage = styleAButtonSelectOnFocusImage;
		break;
	case IMAGE_MENU_BACKGROUND:
		returnImage = menuBackgroundImage;
		break;
	case IMAGE_LOGO:
		returnImage = logoImage;
		break;
	case IMAGE_CONTROLLER_EMPTY:
		returnImage = controllerEmptyImage;
		break;
	case IMAGE_CONTROLLER_GAMECUBE:
		returnImage = controllerGamecubeImage;
		break;
	case IMAGE_CONTROLLER_HID_GAMECUBE:
		returnImage = controllerHidGamecubeImage;
		break;
	case IMAGE_CONTROLLER_CLASSIC:
		returnImage = controllerClassicImage;
		break;
	case IMAGE_CONTROLLER_WIIMOTENUNCHUCK:
		returnImage = controllerWiimoteNunchuckImage;
		break;
	case IMAGE_CONTROLLER_WIIMOTE:
		returnImage = controllerWiimoteImage;
		break;
	case IMAGE_PSX_CONTROLLER:
		returnImage = psxControllerImage;
		break;
	case IMAGE_GCON:
		returnImage = GConImage;
		break;
	case IMAGE_JUST:
		returnImage = JustImage;
		break;
	case IMAGE_PSMOUSE:
		returnImage = PSMouseImage;
		break;	
	}
	return returnImage;
}

} //namespace menu 
