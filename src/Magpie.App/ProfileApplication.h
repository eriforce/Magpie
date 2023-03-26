#pragma once
#include <Magpie.Core.h>

namespace winrt::Magpie::App {

struct ProfileApplication {
	std::wstring GetTruePath() const noexcept;

	bool isPackaged = false;

	// 若为打包应用，PathRule 存储 AUMID
	std::wstring pathRule;
	std::wstring classNameRule;

	std::wstring launchParameters;

	std::wstring truePath;
};

}