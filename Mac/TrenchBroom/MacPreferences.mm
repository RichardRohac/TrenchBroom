/*
 Copyright (C) 2010-2012 Kristian Duske
 
 This file is part of TrenchBroom.
 
 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#import "MacPreferences.h"
#import "Controller/Tool.h"

namespace TrenchBroom {
    namespace Model {
        void MacPreferences::setDictionaryValue(NSMutableDictionary* dict, const string& key, int value) {
            [dict setObject:[NSNumber numberWithInt:value] 
                     forKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }
        
        void MacPreferences::setDictionaryValue(NSMutableDictionary* dict, const string& key, float value) {
            [dict setObject:[NSNumber numberWithFloat:value] 
                     forKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }

        void MacPreferences::setDictionaryValue(NSMutableDictionary* dict, const string& key, const string& value) {
            [dict setObject:[NSString stringWithCString:value.c_str() encoding:NSASCIIStringEncoding] 
                     forKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }
        
        void MacPreferences::setDictionaryValue(NSMutableDictionary* dict, const string& key, const Vec4f& value) {
            setDictionaryValue(dict, key, value.asString());
        }

        int MacPreferences::getInt(const string& key) {
            long value = [[NSUserDefaults standardUserDefaults] integerForKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
            return static_cast<int>(value);
        }
        
        float MacPreferences::getFloat(const string& key) {
            return [[NSUserDefaults standardUserDefaults] floatForKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }
        
        string MacPreferences::getString(const string& key) {
            NSString* value = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
            if (value == nil)
                return "";
            return [value cStringUsingEncoding:NSASCIIStringEncoding];
        }
        
        Vec4f MacPreferences::getVec4f(const string& key) {
            NSString* value = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
            if (value == nil)
                return Vec4f();
            return Vec4f([value cStringUsingEncoding:NSASCIIStringEncoding]);
        }


        void MacPreferences::loadDefaults() {
            Preferences::loadDefaults();

            NSMutableDictionary* dict = [[NSMutableDictionary alloc] init];
            setDictionaryValue(dict, CameraKey, m_cameraKey);
            setDictionaryValue(dict, CameraOrbitKey, m_cameraOrbitKey);
            setDictionaryValue(dict, FaceColor, m_faceColor);
            setDictionaryValue(dict, EdgeColor, m_edgeColor);
            setDictionaryValue(dict, SelectedFaceColor, m_selectedFaceColor);
            setDictionaryValue(dict, SelectedEdgeColor, m_selectedEdgeColor);
            setDictionaryValue(dict, HiddenSelectedEdgeColor, m_hiddenSelectedEdgeColor);
            setDictionaryValue(dict, EntityBoundsColor, m_entityBoundsColor);
            setDictionaryValue(dict, EntityBoundsWireframeColor, m_entityBoundsWireframeColor);
            setDictionaryValue(dict, SelectedEntityBoundsColor, m_selectedEntityBoundsColor);
            setDictionaryValue(dict, HiddenSelectedEntityBoundsColor, m_hiddenSelectedEntityBoundsColor);
            setDictionaryValue(dict, SelectionGuideColor, m_selectionGuideColor);
            setDictionaryValue(dict, HiddenSelectionGuideColor, m_hiddenSelectionGuideColor);
            setDictionaryValue(dict, BackgroundColor, m_backgroundColor);
            setDictionaryValue(dict, InfoOverlayColor, m_infoOverlayColor);
            setDictionaryValue(dict, InfoOverlayFadeDistance, m_infoOverlayFadeDistance);
            setDictionaryValue(dict, SelectedInfoOverlayColor, m_selectedInfoOverlayColor);
            setDictionaryValue(dict, SelectedInfoOverlayFadeDistance, m_selectedInfoOverlayFadeDistance);
            setDictionaryValue(dict, RendererFontName, m_rendererFontName);
            setDictionaryValue(dict, RendererFontSize, m_rendererFontSize);
            setDictionaryValue(dict, GridAlpha, m_gridAlpha);
            setDictionaryValue(dict, QuakePath, m_quakePath);

            NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
            [defaults registerDefaults:dict];
            [dict release];
        }
        
        void MacPreferences::loadPlatformDefaults() {
            m_cameraKey = Controller::TB_MK_SHIFT;
            m_cameraOrbitKey = Controller::TB_MK_SHIFT | Controller::TB_MK_CMD;
            m_quakePath = "/Applications/Quake";
        }
        
        void MacPreferences::loadPreferences() {
            m_cameraKey = getInt(CameraKey);
            m_cameraOrbitKey = getInt(CameraOrbitKey);
            m_faceColor = getVec4f(FaceColor);
            m_edgeColor = getVec4f(EdgeColor);
            m_selectedFaceColor = getVec4f(SelectedFaceColor);
            m_selectedEdgeColor = getVec4f(SelectedEdgeColor);
            m_hiddenSelectedEdgeColor = getVec4f(HiddenSelectedEdgeColor);
            m_entityBoundsColor = getVec4f(EntityBoundsColor);
            m_entityBoundsWireframeColor = getVec4f(EntityBoundsWireframeColor);
            m_selectedEntityBoundsColor = getVec4f(SelectedEntityBoundsColor);
            m_hiddenSelectedEntityBoundsColor = getVec4f(HiddenSelectedEntityBoundsColor);
            m_selectionGuideColor =  getVec4f(SelectionGuideColor);
            m_hiddenSelectionGuideColor = getVec4f(HiddenSelectionGuideColor);
            m_backgroundColor = getVec4f(BackgroundColor);
            m_infoOverlayColor = getVec4f(InfoOverlayColor);
            m_infoOverlayFadeDistance = getFloat(InfoOverlayFadeDistance);
            m_selectedInfoOverlayColor = getVec4f(SelectedInfoOverlayColor);
            m_selectedInfoOverlayFadeDistance = getFloat(SelectedInfoOverlayFadeDistance);
            m_rendererFontName = getString(RendererFontName);
            m_rendererFontSize = getFloat(RendererFontSize);
            m_gridAlpha = getFloat(GridAlpha);
            m_quakePath = getString(QuakePath);
        }
        
        void MacPreferences::saveInt(const string& key, int value) {
            [[NSUserDefaults standardUserDefaults] setInteger:value 
                                                       forKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }
        
        void MacPreferences::saveFloat(const string& key, float value) {
            [[NSUserDefaults standardUserDefaults] setFloat:value 
                                                     forKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }
        
        void MacPreferences::saveString(const string& key, const string& value) {
            [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithCString:value.c_str() encoding:NSASCIIStringEncoding] 
                                                      forKey:[NSString stringWithCString:key.c_str() encoding:NSASCIIStringEncoding]];
        }

        void MacPreferences::saveVec4f(const string& key, const Vec4f& value) {
            saveString(key, value.asString());
        }
    }
}