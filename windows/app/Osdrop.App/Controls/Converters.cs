using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace Osdrop.App.Controls;

/// <summary>True shows the element; with <see cref="Invert"/>, true hides it.</summary>
public sealed class BooleanToVisibility : IValueConverter
{
    public bool Invert { get; set; }

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        value is bool shown && shown != Invert ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

/// <summary>Shows the element while the value is not null.</summary>
public sealed class NullToVisibility : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        value is null ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

/// <summary>Shows the element while every value is false.</summary>
public sealed class AllFalseToVisibility : IMultiValueConverter
{
    public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture) =>
        values.All(value => value is false) ? Visibility.Visible : Visibility.Collapsed;

    public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

/// <summary>Shows the element while a count is zero ("nothing here yet").</summary>
public sealed class ZeroToVisibility : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        value is int count && count == 0 ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
