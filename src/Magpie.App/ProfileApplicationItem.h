#pragma once
#include "ProfileApplicationItem.g.h"
#include "WinRTUtils.h"

namespace winrt::Magpie::App {
struct Profile;
struct ProfileApplication;
}

namespace winrt::Magpie::App::implementation {

struct ProfileApplicationItem : ProfileApplicationItemT<ProfileApplicationItem> {
	ProfileApplicationItem(uint32_t profileIdx, uint32_t applicationIdx);

	event_token PropertyChanged(PropertyChangedEventHandler const& handler) {
		return _propertyChangedEvent.add(handler);
	}

	void PropertyChanged(event_token const& token) noexcept {
		_propertyChangedEvent.remove(token);
	}

	void Launch() const noexcept;

	fire_and_forget OpenProgramLocation() const noexcept;

	void Remove();

	void ApplicationIdx(uint32_t value) noexcept {
		_applicationIdx = value;
	}

	hstring Path() const noexcept {
		return _path;
	}

	bool Exists() const noexcept {
		return _exists;
	}

	Controls::IconElement Icon() const noexcept {
		return _icon;
	}

private:
	void _ProfileService_ApplicationRemoved(uint32_t profileIdx, uint32_t applicationIdx);

	event<PropertyChangedEventHandler> _propertyChangedEvent;

	fire_and_forget _LoadIcon();

	uint32_t _profileIdx = 0;
	uint32_t _applicationIdx = 0;

	ProfileApplication* _application{ nullptr };

	hstring _path;
	bool _exists = false;
	Controls::IconElement _icon{ nullptr };

	WinRTUtils::EventRevoker _applicationRemovedRevoker;
};

}

namespace winrt::Magpie::App::factory_implementation {

struct ProfileApplicationItem : ProfileApplicationItemT<ProfileApplicationItem, implementation::ProfileApplicationItem> {
};

}
