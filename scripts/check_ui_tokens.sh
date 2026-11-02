#!/usr/bin/env bash
# Views take every visual value from the theme (DEVPLAN: theming is centralized). Flags literal
# spacing, sizes, fonts and colours in SwiftUI view code (Theme/ is exempt) and literal visual
# values or text in the Windows shell's XAML (Theme/ is exempt).
set -euo pipefail
cd "$(dirname "$0")/.."

swift_files=$(find macos/Osdrop -name '*.swift' -not -path '*/Theme/*' | sort)
pattern='\.(padding|frame|spacing|cornerRadius|offset)\(([^)]*[^a-zA-Z_.])?[0-9]+(\.[0-9]+)?\b|\.font\(\.(system|title|body|caption|headline|subheadline|largeTitle|footnote)|Color\.(red|green|blue|orange|yellow|gray|black|white|pink|purple)\b|Color\(red:|spacing: [0-9]'
violations=$(grep -nE "$pattern" $swift_files || true)
if [[ -n "$violations" ]]; then
    echo "Literal visual values in views (use Theme tokens):"
    echo "$violations"
    exit 1
fi

xaml=$(find windows/app/Osdrop.App -name '*.xaml' -not -path '*/Theme/*' -not -path '*/obj/*' -not -path '*/bin/*' | sort)
xaml_pattern='\b(Margin|Padding|Width|Height|MinWidth|MinHeight|MaxWidth|FontSize|FontWeight|FontFamily|CornerRadius|BorderThickness|Opacity|Gap)="[^{"]|\b(Foreground|Background|BorderBrush|Fill|Stroke)="[^{"]|\b(Text|Content|Header|Title|ToolTip|ToolTipText|Message)="[^{"]'
violations=$(grep -nE "$xaml_pattern" $xaml || true)
if [[ -n "$violations" ]]; then
    echo "Literal visual values or text in XAML (use Theme/Tokens.xaml and Osdrop.Localization):"
    echo "$violations"
    exit 1
fi
