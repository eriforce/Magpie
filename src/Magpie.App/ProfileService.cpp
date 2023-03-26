#include "pch.h"
#include "ProfileService.h"
#include "Win32Utils.h"
#include "AppSettings.h"
#include "AppXReader.h"
#include <regex>
#include "Logger.h"
#include "StrUtils.h"

namespace winrt::Magpie::App {

static std::wstring_view GetRealClassName(std::wstring_view className) {
	// WPF 窗口类每次启动都会改变，格式为：
	// HwndWrapper[{名称};;{GUID}]
	// GUID 格式为 xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
	static const std::wregex wpfRegex(
		LR"(^HwndWrapper\[(.*);;[0-9,a-f]{8}-[0-9,a-f]{4}-[0-9,a-f]{4}-[0-9,a-f]{4}-[0-9,a-f]{12}\]$)",
		std::wregex::optimize
	);

	std::match_results<std::wstring_view::iterator> matchResults;
	if (std::regex_match(className.begin(), className.end(), matchResults, wpfRegex) && matchResults.size() == 2) {
		return { matchResults[1].first, matchResults[1].second };
	}

	// RPG Maker MZ 制作的游戏每次重新加载（快捷键 F5）窗口类名都会改变，格式为：
	// Chrome_WidgetWin_{递增的数字}
	// 这个类名似乎在基于 Chromium 的程序中很常见，大多数时候是 Chrome_WidgetWin_1
	static const std::wregex rpgMakerMZRegex(LR"(^Chrome_WidgetWin_\d+$)", std::wregex::optimize);
	if (std::regex_match(className.begin(), className.end(), rpgMakerMZRegex)) {
		return L"Chrome_WidgetWin_1";
	}

	return className;
}

static bool RealTestNewProfile(
	bool isPackaged,
	std::wstring_view pathOrAumid,
	std::wstring_view realClassName
) {
	const std::vector<Profile>& profiles = AppSettings::Get().Profiles();

	if (isPackaged) {
		for (const Profile& profile : profiles) {
			for (const ProfileApplication& rule : profile.applications) {
				if (rule.isPackaged && rule.pathRule == pathOrAumid && rule.classNameRule == realClassName) {
					return false;
				}
			}
		}
	} else {
		for (const Profile& profile : profiles) {
			for (const ProfileApplication& rule : profile.applications) {
				if (!rule.isPackaged && rule.GetTruePath() == pathOrAumid && rule.classNameRule == realClassName) {
					return false;
				}
			}
		}
	}

	return true;
}

bool ProfileService::TestNewProfile(bool isPackaged, std::wstring_view pathOrAumid, std::wstring_view className) {
	if (pathOrAumid.empty() || className.empty()) {
		return false;
	}

	return RealTestNewProfile(isPackaged, pathOrAumid, GetRealClassName(className));
}

bool ProfileService::AddProfile(
	bool isPackaged,
	std::wstring_view pathOrAumid,
	std::wstring_view className,
	std::wstring_view name,
	int copyFrom
) {
	assert(!pathOrAumid.empty() && !className.empty() && !name.empty());

	std::wstring_view realClassName = GetRealClassName(className);

	if (!RealTestNewProfile(isPackaged, pathOrAumid, realClassName)) {
		return false;
	}

	std::vector<Profile>& profiles = AppSettings::Get().Profiles();
	Profile& profile = profiles.emplace_back();

	profile.Copy(copyFrom < 0 ? DefaultProfile() : profiles[copyFrom]);

	profile.name = name;

	ProfileApplication& application = profile.applications.emplace_back();
	application.isPackaged = isPackaged;
	application.pathRule = pathOrAumid;
	application.classNameRule = realClassName;

	_profileAddedEvent(std::ref(profile));

	AppSettings::Get().SaveAsync();
	return true;
}

void ProfileService::RenameProfile(uint32_t profileIdx, std::wstring_view newName) {
	assert(!newName.empty());
	AppSettings::Get().Profiles()[profileIdx].name = newName;
	_profileRenamedEvent(profileIdx);
	AppSettings::Get().SaveAsync();
}

void ProfileService::RemoveProfile(uint32_t profileIdx) {
	std::vector<Profile>& profiles = AppSettings::Get().Profiles();
	profiles.erase(profiles.begin() + profileIdx);
	_profileRemovedEvent(profileIdx);
	AppSettings::Get().SaveAsync();
}

bool ProfileService::MoveProfile(uint32_t profileIdx, bool isMoveUp) {
	std::vector<Profile>& profiles = AppSettings::Get().Profiles();
	if (isMoveUp ? profileIdx == 0 : profileIdx + 1 >= (uint32_t)profiles.size()) {
		return false;
	}

	std::swap(profiles[profileIdx], profiles[isMoveUp ? (size_t)profileIdx - 1 : (size_t)profileIdx + 1]);
	_profileReorderedEvent(profileIdx, isMoveUp);

	AppSettings::Get().SaveAsync();
	return true;
}

bool ProfileService::AddApplication(uint32_t profileIdx, bool isPackaged, std::wstring_view pathOrAumid, std::wstring_view className) {
	assert(profileIdx >= 0 && !pathOrAumid.empty() && !className.empty());

	std::wstring_view realClassName = GetRealClassName(className);

	if (!RealTestNewProfile(isPackaged, pathOrAumid, realClassName)) {
		return false;
	}

	Profile& profile = GetProfile(profileIdx);
	ProfileApplication& application = profile.applications.emplace_back();
	application.isPackaged = isPackaged;
	application.pathRule = pathOrAumid;
	application.classNameRule = realClassName;

	uint32_t applicationIdx = static_cast<uint32_t>(profile.applications.size() - 1);
	_applicationAddedEvent(profileIdx, applicationIdx);

	AppSettings::Get().SaveAsync();
	return true;
}

void ProfileService::RemoveApplication(uint32_t profileIdx, uint32_t applicationIdx) {
	Profile& profile = GetProfile(profileIdx);
	profile.applications.erase(profile.applications.begin() + applicationIdx);
	_applicationRemovedEvent(profileIdx, applicationIdx);
	AppSettings::Get().SaveAsync();
}

void ProfileService::MoveApplication(uint32_t profileIdx, uint32_t fromIdx, uint32_t toIdx) {
	std::vector<ProfileApplication>& applications = GetProfile(profileIdx).applications;
	ProfileApplication application = std::move(applications[fromIdx]);
	applications.erase(applications.begin() + fromIdx);
	applications.emplace(applications.begin() + toIdx, std::move(application));
	AppSettings::Get().SaveAsync();
}

Profile& ProfileService::GetProfileForWindow(HWND hWnd) {
	std::wstring className = Win32Utils::GetWndClassName(hWnd);
	std::wstring_view realClassName = GetRealClassName(className);

	AppXReader appXReader;
	if (appXReader.Initialize(hWnd)) {
		// 打包的应用程序匹配 AUMID 和 类名
		const std::wstring& aumid = appXReader.AUMID();
		for (Profile& profile : AppSettings::Get().Profiles()) {
			for (ProfileApplication& rule : profile.applications) {
				if (rule.isPackaged && rule.pathRule == aumid && rule.classNameRule == realClassName) {
					return profile;
				}
			}
		}
	} else {
		// 桌面程序匹配类名和可执行文件名
		std::wstring path = Win32Utils::GetPathOfWnd(hWnd);

		for (Profile& profile : AppSettings::Get().Profiles()) {
			for (ProfileApplication& rule : profile.applications) {
				if (!rule.isPackaged && rule.GetTruePath() == path && rule.classNameRule == realClassName) {
					return profile;
				}
			}
		}
	}

	return DefaultProfile();
}

Profile& ProfileService::DefaultProfile() {
	return AppSettings::Get().DefaultProfile();
}

Profile& ProfileService::GetProfile(uint32_t idx) {
	return AppSettings::Get().Profiles()[idx];
}

uint32_t ProfileService::GetProfileCount() {
	return (uint32_t)AppSettings::Get().Profiles().size();
}

}
