#include "pch.h"
#include "ProfileApplication.h"

std::wstring winrt::Magpie::App::ProfileApplication::GetTruePath() const noexcept {
	return truePath.empty() ? pathRule : truePath;
}
