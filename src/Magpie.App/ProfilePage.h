#pragma once
#include "ProfilePage.g.h"

namespace winrt::Magpie::App::implementation {

struct ProfilePage : ProfilePageT<ProfilePage> {
	ProfilePage();

	void InitializeComponent();

	void OnNavigatedTo(Navigation::NavigationEventArgs const& args);

	Magpie::App::ProfileViewModel ViewModel() const noexcept {
		return _viewModel;
	}

	Magpie::App::NewApplicationViewModel NewAppViewModel() const noexcept {
		return _newApplicationViewModel;
	}

	void ComboBox_DropDownOpened(IInspectable const& sender, IInspectable const&);

	void CursorScalingComboBox_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&);

	static Windows::Globalization::NumberFormatting::INumberFormatter2 NumberFormatter() noexcept;

	void RenameMenuItem_Click(IInspectable const&, RoutedEventArgs const&);

	void RenameFlyout_Opening(IInspectable const&, IInspectable const&);

	void RenameConfirmButton_Click(IInspectable const&, RoutedEventArgs const&);

	void RenameTextBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args);

	void ReorderMenuItem_Click(IInspectable const&, RoutedEventArgs const&);

	void DeleteMenuItem_Click(IInspectable const&, RoutedEventArgs const&);

	void DeleteButton_Click(IInspectable const&, RoutedEventArgs const&);

	void AddApplicationButton_Click(IInspectable const&, RoutedEventArgs const&);

	void NewApplicationConfirmButton_Click(IInspectable const&, RoutedEventArgs const&);

private:
	Magpie::App::ProfileViewModel _viewModel{ nullptr };
	Magpie::App::NewApplicationViewModel _newApplicationViewModel{ nullptr };
	Windows::Globalization::NumberFormatting::DecimalFormatter _numberFormatter;
	Windows::Graphics::Display::DisplayInformation _displayInformation{ nullptr };
};

}

namespace winrt::Magpie::App::factory_implementation {

struct ProfilePage : ProfilePageT<ProfilePage, implementation::ProfilePage> {
};

}
