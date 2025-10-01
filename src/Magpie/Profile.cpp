#include "pch.h"
#include "Profile.h"

namespace Magpie {

std::wstring Profile::GetTruePath() const noexcept {
	return truePath.empty() ? pathRule : truePath;
}

}