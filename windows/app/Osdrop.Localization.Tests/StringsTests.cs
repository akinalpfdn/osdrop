using System.Globalization;
using System.Reflection;
using System.Resources;
using Xunit;

namespace Osdrop.Localization.Tests;

/// <summary>
/// Every user-facing string loads in every shipped language (the shell has no other check). The
/// generated accessor is reached by reflection: it only exists after a build, and `dotnet format`
/// analyzes the sources before one.
/// </summary>
public sealed class StringsTests
{
    private static readonly Type _strings =
        Type.GetType("Osdrop.Localization.Strings, Osdrop.Localization", throwOnError: true)!;

    private static readonly PropertyInfo[] _properties = _strings
        .GetProperties(BindingFlags.Public | BindingFlags.Static)
        .Where(property => property.PropertyType == typeof(string))
        .ToArray();

    [Theory]
    [InlineData("en")]
    [InlineData("tr")]
    public void LoadsEveryStringInEachShippedCulture(string culture)
    {
        PropertyInfo cultureProperty = _strings.GetProperty("Culture", BindingFlags.Public | BindingFlags.Static)!;
        cultureProperty.SetValue(null, CultureInfo.GetCultureInfo(culture));
        try
        {
            Assert.NotEmpty(_properties);
            foreach (PropertyInfo property in _properties)
            {
                Assert.False(string.IsNullOrWhiteSpace((string?)property.GetValue(null)), property.Name);
            }
        }
        finally
        {
            cultureProperty.SetValue(null, null);
        }
    }

    [Fact]
    public void TranslatesEveryStringIntoTurkish()
    {
        var manager = (ResourceManager)_strings
            .GetProperty("ResourceManager", BindingFlags.Public | BindingFlags.Static)!
            .GetValue(null)!;
        ResourceSet? turkish = manager.GetResourceSet(CultureInfo.GetCultureInfo("tr"), true, false);
        Assert.NotNull(turkish);
        foreach (PropertyInfo property in _properties)
        {
            Assert.NotNull(turkish.GetString(property.Name));
        }
    }
}
