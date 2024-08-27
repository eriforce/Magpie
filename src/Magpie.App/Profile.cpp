#include "pch.h"
#include "Profile.h"

std::wstring winrt::Magpie::App::Profile::GetTruePath() const noexcept {
	return truePath.empty() ? pathRule : truePath;
}