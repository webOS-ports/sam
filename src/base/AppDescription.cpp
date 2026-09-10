// Copyright (c) 2012-2025 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <glib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "base/AppDescription.h"
#include "bus/client/SettingService.h"
#include "conf/SAMConf.h"
#include "util/JValueUtil.h"
#include "util/File.h"

const vector<string> AppDescription::PROPS_PROHIBITED = {
    "id", "type", "trustLevel"
};
const vector<string> AppDescription::ASSETS_SUPPORTED = {
    "icon", "largeIcon", "bgImage", "splashBackground"
};
const vector<string> AppDescription::PROPS_IMAGES = {
    "icon", "largeIcon", "bgImage", "splashBackground", "miniicon", "mediumIcon", "splashicon", "imageForRecents"
};
const vector<string> AppDescription::PROPS_PATHS = {
    "main"
};

const string AppDescription::CLASS_NAME = "AppDescription";

string AppDescription::toString(const AppStatusEvent& event)
{
    switch (event) {
    case AppStatusEvent::AppStatusEvent_Nothing:
        return "";
        break;

    case AppStatusEvent::AppStatusEvent_Installed:
        return "appInstalled";
        break;

    case AppStatusEvent::AppStatusEvent_Uninstalled:
        return "appUninstalled";
        break;

    case AppStatusEvent::AppStatusEvent_UpdateCompleted:
        return "updateCompleted";
        break;

    default:
        return "unknown";
        break;
    }
}

string AppDescription::toString(AppType type)
{
    string str;
    switch (type) {
    case AppType::AppType_Web:
        str = "web";
        break;

    case AppType::AppType_Stub:
        str = "stub";
        break;

    case AppType::AppType_Native:
        str = "native";
        break;

    case AppType::AppType_Native_Builtin:
        str = "native_builtin";
        break;

    case AppType::AppType_Native_Mvpd:
        str = "native_mvpd";
        break;

    case AppType::AppType_Native_Qml:
        str = "native_qml";
        break;

    case AppType::AppType_Native_AppShell:
        str = "native_appshell";
        break;

    case AppType::AppType_Native_BrowserShell:
        str = "native_browsershell";
        break;

    default:
        str = "unknown";
        break;
    }
    return str;
}

AppType AppDescription::toAppType(const string& type)
{
    if (type == "web") {
        return AppType::AppType_Web;
    } else if (type == "stub") {
        return AppType::AppType_Stub;
    } else if (type == "native") {
        return AppType::AppType_Native;
    } else if (type == "native_builtin") {
        return AppType::AppType_Native_Builtin;
    } else if (type == "native_mvpd") {
        return AppType::AppType_Native_Mvpd;
    } else if (type == "native_qml" || type == "qml") {
        return AppType::AppType_Native_Qml;
    } else if (type == "native_appshell") {
        return AppType::AppType_Native_AppShell;
    } else if (type == "native_browsershell") {
        return AppType::AppType_Native_BrowserShell;
    }
    return AppType::AppType_None;
}

const char* AppDescription::toString(AppLocation location)
{
    switch (location) {
    case AppLocation::AppLocation_Devmode:
        return "web";

    case AppLocation::AppLocation_AppStore_Internal:
        return "store_internal";

    case AppLocation::AppLocation_AppStore_External:
        return "store_external";

    case AppLocation::AppLocation_System_ReadWrite:
        return "system_updatable";

    case AppLocation::AppLocation_System_ReadOnly:
        return "system_builtin";

    default:
        return "unknown";
    }
}

AppLocation AppDescription::toAppLocation(const string& type)
{
    if (type == "store" || type == "store_internal") {
        return AppLocation::AppLocation_AppStore_Internal;
    } else if (type == "store_external") { // TODO
        return AppLocation::AppLocation_AppStore_External;
    } else if (type == "system_updatable") {
        return AppLocation::AppLocation_System_ReadWrite;
    } else if (type == "system_builtin") {
        return AppLocation::AppLocation_System_ReadOnly;
    } else if (type == "dev") {
        return AppLocation::AppLocation_Devmode;
    } else {
        return AppLocation::AppLocation_None;
    }
}

AppDescription::AppDescription(const string& appId)
    : m_appLocation(AppLocation::AppLocation_None),
      m_folderPath(""),
      m_appId(appId),
      m_appType(AppType::AppType_None),
      m_intVersion(1, 0, 0),
      m_absMain(""),
      m_absSplashBackground(""),
      m_isLocked(false),
      m_isScanned(false)
{
}

AppDescription::~AppDescription()
{
}

bool AppDescription::scan()
{
    m_isScanned = false;
    if (m_appId.empty() || m_folderPath.empty() || m_appLocation == AppLocation::AppLocation_None) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, "Required members are not set");
        return false;
    }

    if (!File::isDirectory(m_folderPath)) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, "FolderPath is not exist");
        return false;
    }

    if (!isAllowedAppId()) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, "AppId is not allowed");
        return false;
    }

    if (!loadAppinfo() || !readAppinfo() || !readAsset()) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, "Cannot configure AppDescription");
        return false;
    }

    m_isScanned = true;
    return true;
}

bool AppDescription::scan(const string& folderPath, const AppLocation& appLocation)
{
    Logger::debug(CLASS_NAME, __FUNCTION__, m_appId,
                  Logger::format("folderPath(%s) appLocation(%s)", folderPath.c_str(), toString(appLocation)));
    m_folderPath = folderPath;
    m_appLocation = appLocation;
    return scan();
}

void AppDescription::applyFolderPath(string& path)
{
    if (path.compare(0, 7, "file://") == 0)
        path.erase(0, 7);

    if (path.empty() || path[0] == '/')
        return;

    if (m_folderPath.empty())
        return;

    path = File::join(m_folderPath, path);
}

JValue AppDescription::getJson(JValue& properties)
{
    if (properties.isNull() || !properties.isArray()) {
        return pbnjson::Object();
    }

    if (properties.arraySize() == 0)
        return m_appinfo;

    JValue result = pbnjson::Object();
    JValue notSpecified = pbnjson::Array();

    // get selected props from appinfo
    for (int i = 0; i < properties.arraySize(); ++i) {
        string property = "";
        if (!properties[i].isString() || properties[i].asString(property) != CONV_OK)
            continue;
        if (m_appinfo.hasKey(property))
            result.put(property, m_appinfo[property]);
        else
            JValueUtil::addUniqueItemToArray(notSpecified, property);
    }

    if (notSpecified.arraySize() > 0)
        result.put("notSpecified", notSpecified);

    return result;
}

// Anchor a path taken from a localized appinfo.json against the directory it is
// really relative to, and hand it back relative to the application folder.
//
// Two conventions are in use and they need opposite handling:
//
//   Legacy Palm/HP applications write the path relative to the localization
//   dir - com.palm.app.accounts has "main": "../../index.html" and
//   "icon": "../../icon.png" - so it has to be re-anchored against that dir.
//
//   The webOS OSE applications write it relative to the application root -
//   every com.webos.app.* Enact application has "main": "index.multi.html" -
//   and re-anchoring those yields resources/<locale>/index.multi.html, which
//   does not exist. SAM then hands WAM an entry point it cannot stat, and the
//   application comes up as an empty card.
//
// Rather than guess from the shape of the value, form both candidates and keep
// whichever is actually on disk, preferring the localization-relative one so a
// genuinely localized asset still wins over a same-named file at the root.
//
// The result must not start with '/': applyFolderPath() takes a leading slash
// to mean the path is already absolute and returns without prefixing
// m_folderPath, so an icon anchored as "/icon.png" - or, before this was
// canonicalised, "/resources/en/../../icon.png" - is published as-is and
// resolves nowhere. That is why com.palm.app.accounts had a working main and no
// icon in the launcher.
string AppDescription::anchorLocalePath(const string& relativeLocaleDir, const string& value)
{
    auto relativise = [](const string& path) -> string {
        gchar* resolved = g_canonicalize_filename(path.c_str(), "/");
        string result = (resolved != nullptr) ? resolved : path;
        if (resolved != nullptr)
            g_free(resolved);
        if (!result.empty() && result[0] == '/')
            result.erase(0, 1);
        return result;
    };

    string localeRelative = relativise(relativeLocaleDir + value);
    if (File::isFile(File::join(m_folderPath, localeRelative)))
        return localeRelative;

    string rootRelative = relativise(value);
    if (File::isFile(File::join(m_folderPath, rootRelative)))
        return rootRelative;

    // Neither is present. Keep the localization-relative form so a value naming
    // something generated later is not silently rewritten.
    return localeRelative;
}

bool AppDescription::loadAppinfo()
{
    // Specify application description depending on available locale string.
    // This string is in BCP-47 format which can contain a language, language-region,
    // or language-region-script. When SAM loads the appinfo.json file,
    // it should look in resources/<language>/appinfo.json,
    // resources/<language>/<region>/appinfo.json,
    // or resources/<language>/<script>/<region>/appinfo.json respectively.
    // (Note that the script dir goes in between the language and region dirs.)
    const string appinfoPath = File::join(m_folderPath, "/appinfo.json");
    m_appinfo = JDomParser::fromFile(appinfoPath.c_str(), JValueUtil::getSchema("ApplicationDescription"));
    if (!isValidAppInfo(m_appinfo)) {
        Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, Logger::format("Failed to parse appinfo.json(%s)", appinfoPath.c_str()));
        m_appinfo = pbnjson::JValue();
        return false;
    }

    /// Add folderPath to JSON
    m_appinfo.put("folderPath", m_folderPath);

    vector<string> localizationDirs;
    string resourcePath = m_folderPath + "/resources/" + SAMConf::getInstance().getLanguage() + "/";
    localizationDirs.push_back(resourcePath);

    // Skip empty locale components: for a locale without a script (e.g. en-US)
    // the script dir would duplicate the language dir with a trailing "//",
    // applying the same localized appinfo.json twice and corrupting
    // re-anchored relative paths.
    if (!SAMConf::getInstance().getScript().empty()) {
        resourcePath += SAMConf::getInstance().getScript() + "/";
        localizationDirs.push_back(resourcePath);
    }

    if (!SAMConf::getInstance().getRegion().empty()) {
        resourcePath += SAMConf::getInstance().getRegion() + "/";
        localizationDirs.push_back(std::move(resourcePath));
    }

    // apply localization (overwrite from low to high)
    for (const auto& localizationDir : localizationDirs) {
        string AbsoluteLocaleAppinfoPath = localizationDir + "appinfo.json";
        string RelativeLocaleAppinfoPath = localizationDir.substr(m_folderPath.length());

        if (!File::isFile(AbsoluteLocaleAppinfoPath)) {
            continue;
        }

        JValue localeAppinfo = JDomParser::fromFile(AbsoluteLocaleAppinfoPath.c_str());
        if (localeAppinfo.isNull()) {
            Logger::info(CLASS_NAME, __FUNCTION__, "IGNORRED", Logger::format("failed_to_load_localication: %s", localizationDir.c_str()));
            continue;
        }

        for (auto item : localeAppinfo.children()) {
            string key = item.first.asString();

            if (!m_appinfo.hasKey(key) || m_appinfo[key].getType() != localeAppinfo[key].getType()) {
                Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, AbsoluteLocaleAppinfoPath, "localization is unmatchted with root");
                continue;
            }

            if (m_appinfo[key] == localeAppinfo[key]) {
                continue;
            }

            if (find(PROPS_PROHIBITED.begin(), PROPS_PROHIBITED.end(), key) != PROPS_PROHIBITED.end()) {
                Logger::warning(CLASS_NAME, __FUNCTION__, m_appId, AbsoluteLocaleAppinfoPath, "localization is prohibited_props");
                continue;
            }

            if (find(ASSETS_SUPPORTED.begin(), ASSETS_SUPPORTED.end(), key) != ASSETS_SUPPORTED.end()) {
                // check asset variation rule with root value
                string baseAssetValue = m_appinfo[key].asString();
                string localeAssetValue = localeAppinfo[key].asString();

                // if assets variation rule is specified, dont support localization
                if (baseAssetValue.length() > 0 && baseAssetValue[0] == '$')
                    continue;
                if (localeAssetValue.length() > 0 && localeAssetValue[0] == '$')
                    continue;
            }

            if (find(PROPS_IMAGES.begin(), PROPS_IMAGES.end(), key) != PROPS_IMAGES.end() ||
                find(PROPS_PATHS.begin(), PROPS_PATHS.end(), key) != PROPS_PATHS.end()) {
                m_appinfo.put(key, anchorLocalePath(RelativeLocaleAppinfoPath,
                                                    localeAppinfo[key].asString()));
            } else {
                m_appinfo.put(key, localeAppinfo[key]);
            }
        }
    }
    return true;
}

bool AppDescription::readAppinfo()
{
    if (!isValidAppInfo(m_appinfo))
        return false;

    // entry_point
    JValueUtil::getValue(m_appinfo, "main", m_absMain);
    if (!strstr(m_absMain.c_str(), "://"))
        m_absMain = string("file://") + m_folderPath + string("/") + m_absMain;

    // splash_background
    JValueUtil::getValue(m_appinfo, "splashBackground", m_absSplashBackground);
    if (!strstr(m_absSplashBackground.c_str(), "://"))
        m_absSplashBackground = string("file://") + m_folderPath + string("/") + m_absSplashBackground;

    // version
    string version = "1.0.0";
    JValueUtil::getValue(m_appinfo, "version", version);
    vector<string> versionInfo;
    boost::split(versionInfo, version, boost::is_any_of("."));
    // version comes from a third-party appinfo.json, so a component may be
    // anything at all. stoi threw std::invalid_argument out of the whole scan;
    // treat an unparseable component as 0 the way a missing one already was.
    auto versionPart = [&versionInfo](size_t index) -> uint16_t {
        uint16_t part = 0;
        if (index < versionInfo.size())
            boost::conversion::try_lexical_convert(versionInfo[index], part);
        return part;
    };
    uint16_t major_ver = versionPart(0);
    uint16_t minor_ver = versionPart(1);
    uint16_t micro_ver = versionPart(2);
    m_intVersion = { major_ver, minor_ver, micro_ver };

    // app_type
    bool privilegedJail = false;
    JValueUtil::getValue(m_appinfo, "privilegedJail", privilegedJail);
    m_appType = toAppType(m_appinfo["type"].asString());

    if (m_appType == AppType::AppType_Native && privilegedJail)
        m_appType = AppType::AppType_Native_Mvpd;

    if (isSystemApp()) {
        m_appinfo.put("systemApp", true);
        m_appinfo.put("removable", false);
    } else {
        m_appinfo.put("systemApp", false);
    }

    if (AppLocation::AppLocation_Devmode == m_appLocation) {
        m_appinfo.put("inspectable", true);
    }
    return true;
}

bool AppDescription::readAsset()
{
    string sysAssetsBasePath = "sys-assets";
    JValueUtil::getValue(m_appinfo, "sysAssetsBasePath", sysAssetsBasePath);

    for (const auto& key : ASSETS_SUPPORTED) {
        string value;

        if (!JValueUtil::getValue(m_appinfo, key, value) || value.empty()) {
            continue;
        }

        // check if it requests assets control finding symbol '$'
        if (value.length() < 2 || value[0] != '$') {
            continue;
        }

        string filename = value.substr(1);
        bool foundAsset = false;

        JValue fallbacks = SAMConf::getInstance().getSysAssetFallbackPrecedence();
        for (int i = 0; i < fallbacks.arraySize(); i++) {
            string assetPath = File::join(File::join(sysAssetsBasePath, fallbacks[i].asString()), filename);
            string pathToCheck = "";

            // set asset without variant
            pathToCheck = m_folderPath + string("/") + assetPath;
            Logger::debug(CLASS_NAME, __FUNCTION__, Logger::format("patch_to_check: %s\n", pathToCheck.c_str()));

            if (0 == access(pathToCheck.c_str(), F_OK)) {
                m_appinfo.put(key, assetPath);
                foundAsset = true;
                break;
            }
        }

        if (foundAsset) {
            continue;
        }

        string defaultAsset = File::join(sysAssetsBasePath, filename);
        m_appinfo.put(key, defaultAsset);
    }
    return true;
}

