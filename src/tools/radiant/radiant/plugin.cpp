/**
 * @file plugin.cpp
 */

/*
 Copyright (C) 2001-2006, William Joseph.
 All Rights Reserved.

 This file is part of GtkRadiant.

 GtkRadiant is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 GtkRadiant is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with GtkRadiant; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <iostream>
#include "plugin.h"

#include "debugging/debugging.h"

#include "iradiant.h"
#include "ifilesystem.h"
#include "ishaders.h"
#include "ientity.h"
#include "ieclass.h"
#include "irender.h"
#include "iscenegraph.h"
#include "iselection.h"
#include "ifilter.h"
#include "iscriplib.h"
#include "igl.h"
#include "iundo.h"
#include "itextures.h"
#include "ireference.h"
#include "ifiletypes.h"
#include "preferencesystem.h"
#include "ibrush.h"
#include "iimage.h"
#include "itoolbar.h"
#include "iplugin.h"
#include "imap.h"
#include "namespace.h"
#include "commands.h"

#include "gtkutil/messagebox.h"
#include "gtkutil/filechooser.h"
#include "maplib.h"

#include "error.h"
#include "map.h"
#include "qe3.h"
#include "sidebar/sidebar.h"
#include "gtkmisc.h"
#include "mainframe.h"
#include "lastused.h"
#include "camwindow.h"
#include "xywindow.h"
#include "entity.h"
#include "select.h"
#include "preferences.h"
#include "autosave.h"
#include "plugintoolbar.h"
#include "dialogs/findtextures.h"
#include "nullmodel.h"
#include "grid.h"
#include "material.h"
#include "particles.h"

#include "modulesystem/modulesmap.h"
#include "modulesystem/singletonmodule.h"

#include "generic/callback.h"

#include "exception/RadiantException.h"

const char* GameDescription_getKeyValue (const char* key)
{
	return g_pGameDescription->getKeyValue(key);
}

const char* GameDescription_getRequiredKeyValue (const char* key)
{
	return g_pGameDescription->getRequiredKeyValue(key);
}

const char* getMapName ()
{
	return Map_Name(g_map);
}

scene::Node& getMapWorldEntity ()
{
	return Map_FindOrInsertWorldspawn(g_map);
}

VIEWTYPE XYWindow_getViewType ()
{
	return g_pParentWnd->GetXYWnd()->GetViewType();
}

Vector3 XYWindow_windowToWorld (const WindowVector& position)
{
	Vector3 result(0, 0, 0);
	g_pParentWnd->GetXYWnd()->XY_ToPoint(static_cast<int> (position.x()), static_cast<int> (position.y()), result);
	return result;
}

const char* TextureBrowser_getSelectedShader ()
{
	return TextureBrowser_GetSelectedShader(GlobalTextureBrowser());
}

class RadiantCoreAPI
{
		IRadiant m_radiantcore;
	public:
		typedef IRadiant Type;
		STRING_CONSTANT(Name, "*");

		RadiantCoreAPI ()
		{
			m_radiantcore.getMainWindow = MainFrame_getWindow;
			m_radiantcore.getEnginePath = &EnginePath_get;
			m_radiantcore.getAppPath = &AppPath_get;
			m_radiantcore.getSettingsPath = &SettingsPath_get;
			m_radiantcore.getMapsPath = &getMapsPath;
			m_radiantcore.commandInsert = &GlobalCommands_insert;

			m_radiantcore.getGameName = &gamename_get;

			m_radiantcore.getMapName = &getMapName;
			m_radiantcore.getMapWorldEntity = getMapWorldEntity;
			m_radiantcore.getGridSize = GetGridSize;

			m_radiantcore.getGameDescriptionKeyValue = &GameDescription_getKeyValue;
			m_radiantcore.getRequiredGameDescriptionKeyValue = &GameDescription_getRequiredKeyValue;

			m_radiantcore.attachGameToolsPathObserver = Radiant_attachGameToolsPathObserver;
			m_radiantcore.detachGameToolsPathObserver = Radiant_detachGameToolsPathObserver;
			m_radiantcore.attachEnginePathObserver = Radiant_attachEnginePathObserver;
			m_radiantcore.detachEnginePathObserver = Radiant_detachEnginePathObserver;
			m_radiantcore.attachGameNameObserver = Radiant_attachGameNameObserver;
			m_radiantcore.detachGameNameObserver = Radiant_detachGameNameObserver;
			m_radiantcore.attachGameModeObserver = Radiant_attachGameModeObserver;
			m_radiantcore.detachGameModeObserver = Radiant_detachGameModeObserver;

			m_radiantcore.XYWindowDestroyed_connect = XYWindowDestroyed_connect;
			m_radiantcore.XYWindowDestroyed_disconnect = XYWindowDestroyed_disconnect;
			m_radiantcore.XYWindowMouseDown_connect = XYWindowMouseDown_connect;
			m_radiantcore.XYWindowMouseDown_disconnect = XYWindowMouseDown_disconnect;
			m_radiantcore.XYWindow_getViewType = XYWindow_getViewType;
			m_radiantcore.XYWindow_windowToWorld = XYWindow_windowToWorld;
			m_radiantcore.TextureBrowser_getSelectedShader = TextureBrowser_getSelectedShader;

			m_radiantcore.m_pfnMessageBox = &gtk_MessageBox;
			m_radiantcore.m_pfnFileDialog = &file_dialog;
			m_radiantcore.m_pfnColorDialog = &color_dialog;
			m_radiantcore.m_pfnDirDialog = &dir_dialog;
			m_radiantcore.m_pfnNewImage = &new_plugin_image;
		}
		IRadiant* getTable ()
		{
			return &m_radiantcore;
		}
};

typedef SingletonModule<RadiantCoreAPI> RadiantCoreModule;
typedef Static<RadiantCoreModule> StaticRadiantCoreModule;
StaticRegisterModule staticRegisterRadiantCore (StaticRadiantCoreModule::instance ());

class RadiantDependencies: public GlobalRadiantModuleRef,
		public GlobalFileSystemModuleRef,
		public GlobalEntityModuleRef,
		public GlobalShadersModuleRef,
		public GlobalBrushModuleRef,
		public GlobalSceneGraphModuleRef,
		public GlobalShaderCacheModuleRef,
		public GlobalFiletypesModuleRef,
		public GlobalSelectionModuleRef,
		public GlobalReferenceModuleRef,
		public GlobalOpenGLModuleRef,
		public GlobalEntityClassManagerModuleRef,
		public GlobalUndoModuleRef,
		public GlobalScripLibModuleRef,
		public GlobalNamespaceModuleRef
{
		ImageModulesRef m_image_modules;
		MapModulesRef m_map_modules;
		ToolbarModulesRef m_toolbar_modules;
		PluginModulesRef m_plugin_modules;

	public:
		RadiantDependencies () :
			GlobalEntityModuleRef("ufo"), GlobalShadersModuleRef("ufo"), GlobalBrushModuleRef("ufo"),
					GlobalEntityClassManagerModuleRef("ufo"), m_image_modules(
							GlobalRadiant().getRequiredGameDescriptionKeyValue("texturetypes")),
					m_map_modules("mapufo"), m_toolbar_modules("*"), m_plugin_modules("*")
		{
		}

		ImageModules& getImageModules ()
		{
			return m_image_modules.get();
		}
		MapModules& getMapModules ()
		{
			return m_map_modules.get();
		}
		ToolbarModules& getToolbarModules ()
		{
			return m_toolbar_modules.get();
		}
		PluginModules& getPluginModules ()
		{
			return m_plugin_modules.get();
		}
};

class Radiant: public TypeSystemRef
{
	public:
		Radiant ()
		{
			Preferences_Init();

			/** @todo Add soundtypes support into ufoai.game */
			GlobalFiletypes().addType("sound", "wav", filetype_t("PCM sound files", "*.wav"));
			GlobalFiletypes().addType("sound", "ogg", filetype_t("OGG sound files", "*.ogg"));

			Selection_Construct();
			HomePaths_Construct();
			VFS_Construct();
			Grid_Construct();
			MRU_Construct();
			GLWindow_Construct();
			Map_Construct();
			EntityList_Construct();
			MapInfo_Construct();
			JobInfo_Construct();
			MainFrame_Construct();
			SurfaceInspector_Construct();
			CamWnd_Construct();
			XYWindow_Construct();
			TextureBrowser_Construct();
			ParticleBrowser_Construct();
			Entity_Construct();
			Autosave_Construct();
			EntityInspector_Construct();
			FindTextureDialog_Construct();
			NullModel_Construct();
			MapRoot_Construct();
			Material_Construct();

			EnginePath_verify();
			EnginePath_Realise();

			Particles_Construct();
		}
		~Radiant ()
		{
			Particles_Destroy();

			EnginePath_Unrealise();

			Material_Destroy();
			MapRoot_Destroy();
			NullModel_Destroy();
			FindTextureDialog_Destroy();
			EntityInspector_Destroy();
			Autosave_Destroy();
			Entity_Destroy();
			ParticleBrowser_Destroy();
			TextureBrowser_Destroy();
			XYWindow_Destroy();
			CamWnd_Destroy();
			SurfaceInspector_Destroy();
			MainFrame_Destroy();
			EntityList_Destroy();
			MapInfo_Destroy();
			JobInfo_Destroy();
			Map_Destroy();
			GLWindow_Destroy();
			MRU_Destroy();
			Grid_Destroy();
			VFS_Destroy();
			HomePaths_Destroy();
			Selection_Destroy();
		}
};

namespace
{
	bool g_RadiantInitialised = false;
	RadiantDependencies* g_RadiantDependencies;
	Radiant* g_Radiant;
}

bool Radiant_Construct (ModuleServer& server)
{
	GlobalModuleServer::instance().set(server);
	StaticModuleRegistryList().instance().registerModules();

	g_RadiantDependencies = new RadiantDependencies();

	g_RadiantInitialised = !server.getError();

	if (g_RadiantInitialised) {
		g_Radiant = new Radiant;
	} else {
		throw RadiantException("Radiant_Construct: Failed to initialise Radiant");
	}

	return g_RadiantInitialised;
}
void Radiant_Destroy ()
{
	if (g_RadiantInitialised) {
		delete g_Radiant;
	}

	delete g_RadiantDependencies;
}

ImageModules& Radiant_getImageModules ()
{
	return g_RadiantDependencies->getImageModules();
}
MapModules& Radiant_getMapModules ()
{
	return g_RadiantDependencies->getMapModules();
}
ToolbarModules& Radiant_getToolbarModules ()
{
	return g_RadiantDependencies->getToolbarModules();
}
PluginModules& Radiant_getPluginModules ()
{
	return g_RadiantDependencies->getPluginModules();
}
