#pragma once
#include "ProfileViewModel.g.h"
#include "SmallVector.h"
#include "WinRTUtils.h"

namespace winrt::Magpie::App {
struct Profile;
struct ProfileApplication;
}

namespace winrt::Magpie::App::implementation {

struct ProfileViewModel : ProfileViewModelT<ProfileViewModel> {
	ProfileViewModel(int profileIdx);
	~ProfileViewModel();

	event_token PropertyChanged(PropertyChangedEventHandler const& handler) {
		return _propertyChangedEvent.add(handler);
	}

	void PropertyChanged(event_token const& token) noexcept {
		_propertyChangedEvent.remove(token);
	}

	bool IsNotDefaultProfile() const noexcept;

	hstring Name() const noexcept;

	hstring RenameText() const noexcept {
		return _renameText;
	}

	void RenameText(const hstring& value);

	bool IsRenameConfirmButtonEnabled() const noexcept {
		return _isRenameConfirmButtonEnabled;
	}

	void Rename();

	bool CanMoveUp() const noexcept;

	bool CanMoveDown() const noexcept;

	void MoveUp();

	void MoveDown();

	void Delete();

	IObservableVector<IInspectable> Applications() const noexcept {
		return _applications;
	}

	IVector<IInspectable> ScalingModes() const noexcept {
		return _scalingModes;
	}

	int ScalingMode() const noexcept;
	void ScalingMode(int value);

	IVector<IInspectable> CaptureMethods() const noexcept {
		return _captureMethods;
	}

	int CaptureMethod() const noexcept;
	void CaptureMethod(int value);

	bool IsAutoScale() const noexcept;
	void IsAutoScale(bool value);

	bool Is3DGameMode() const noexcept;
	void Is3DGameMode(bool value);

	bool HasMultipleMonitors() const noexcept;

	int MultiMonitorUsage() const noexcept;
	void MultiMonitorUsage(int value);

	IVector<IInspectable> GraphicsCards() const noexcept;

	int GraphicsCard() const noexcept;
	void GraphicsCard(int value);

	bool IsShowGraphicsCardSettingsCard() const noexcept;

	bool IsShowFPS() const noexcept;
	void IsShowFPS(bool value);

	bool IsVSync() const noexcept;
	void IsVSync(bool value);

	bool IsTripleBuffering() const noexcept;
	void IsTripleBuffering(bool value);

	bool IsDisableWindowResizing() const noexcept;
	void IsDisableWindowResizing(bool value);

	bool IsCaptureTitleBar() const noexcept;
	void IsCaptureTitleBar(bool value);

	bool CanCaptureTitleBar() const noexcept;

	bool IsCroppingEnabled() const noexcept;
	void IsCroppingEnabled(bool value);

	double CroppingLeft() const noexcept;
	void CroppingLeft(double value);

	double CroppingTop() const noexcept;
	void CroppingTop(double value);

	double CroppingRight() const noexcept;
	void CroppingRight(double value);

	double CroppingBottom() const noexcept;
	void CroppingBottom(double value);

	bool IsAdjustCursorSpeed() const noexcept;
	void IsAdjustCursorSpeed(bool value);

	bool IsDrawCursor() const noexcept;
	void IsDrawCursor(bool value);

	int CursorScaling() const noexcept;
	void CursorScaling(int value);

	double CustomCursorScaling() const noexcept;
	void CustomCursorScaling(double value);

	int CursorInterpolationMode() const noexcept;
	void CursorInterpolationMode(int value);

	bool IsDisableDirectFlip() const noexcept;
	void IsDisableDirectFlip(bool value);

private:
	void _ProfileService_ApplicationAdded(uint32_t profileIdx, uint32_t applicationIdx);
	void _ProfileService_ApplicationRemoved(uint32_t profileIdx, uint32_t applicationIdx);

	void _Applications_VectorChanged(IObservableVector<IInspectable> const&, IVectorChangedEventArgs const& args);

	void _MagService_Is3DGameModeChanged(bool value);

	hstring _renameText;
	std::wstring_view _trimedRenameText;

	IObservableVector<IInspectable> _applications = single_threaded_observable_vector<IInspectable>();
	IVector<IInspectable> _scalingModes{ nullptr };
	IVector<IInspectable> _captureMethods{ nullptr };
	SmallVector<std::wstring> _graphicsCards;

	event<PropertyChangedEventHandler> _propertyChangedEvent;

	uint32_t _index = 0;
	// 可以保存此指针的原因是：用户停留在此页面时不会有缩放配置被创建或删除
	Profile* _data = nullptr;

	MainPage::ActualThemeChanged_revoker _themeChangedRevoker;
	Windows::Graphics::Display::DisplayInformation _displayInformation{ nullptr };
	Windows::Graphics::Display::DisplayInformation::DpiChanged_revoker _dpiChangedRevoker;
	WinRTUtils::EventRevoker _applicationAddedRevoker;
	WinRTUtils::EventRevoker _applicationRemovedRevoker;
	WinRTUtils::EventRevoker _is3DGameModeChangedRevoker;

	const bool _isDefaultProfile = true;
	bool _isRenameConfirmButtonEnabled = false;
	bool _isMovingApplication = true;
	uint32_t _applicationMovingFromIdx = 0;
};

}

namespace winrt::Magpie::App::factory_implementation {

struct ProfileViewModel : ProfileViewModelT<ProfileViewModel, implementation::ProfileViewModel> {
};

}
