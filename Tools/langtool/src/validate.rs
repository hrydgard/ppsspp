use regex::Regex;
use std::fmt;
use std::sync::LazyLock;

// %1-%9 are positional placeholders, %s/%d/%f are printf-style ones. Either way they need to
// survive translation intact, or we get garbage (or worse) on screen at runtime.
static PLACEHOLDER: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"%[0-9sdf]").unwrap());

#[derive(Debug, PartialEq, Eq)]
pub enum Issue {
    Placeholders { expected: String, found: String },
    Empty,
    Newline,
    Quoted,
    Untranslated,
}

impl fmt::Display for Issue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Issue::Placeholders { expected, found } => {
                write!(f, "placeholder mismatch, expected [{expected}], got [{found}]")
            }
            Issue::Empty => write!(f, "empty translation"),
            Issue::Newline => write!(f, "contains a line break"),
            Issue::Quoted => write!(f, "wrapped in quotes or a markdown fence"),
            Issue::Untranslated => write!(f, "same as the English string"),
        }
    }
}

fn placeholders(str: &str) -> Vec<&str> {
    let mut found: Vec<&str> = PLACEHOLDER.find_iter(str).map(|m| m.as_str()).collect();
    found.sort_unstable();
    found
}

/// Checks a translated string against the English one it was translated from. Cheap enough to
/// run over every string in every language file, see the Validate command.
///
/// Note that Untranslated is never returned here - plenty of strings are legitimately identical
/// to the English one, so only the AI paths check for it (see check_ai_translation).
pub fn check(reference: &str, translation: &str) -> Vec<Issue> {
    let mut issues = vec![];

    if translation.trim().is_empty() {
        // Nothing else is going to be meaningful.
        return vec![Issue::Empty];
    }

    if translation.contains('\n') || translation.contains('\r') {
        issues.push(Issue::Newline);
    }

    let expected = placeholders(reference);
    let found = placeholders(translation);
    if expected != found {
        issues.push(Issue::Placeholders {
            expected: expected.join(" "),
            found: found.join(" "),
        });
    }

    // The AI likes to quote things, and the quotes are not part of the translation.
    if is_quoted(translation.trim()) && !is_quoted(reference.trim()) {
        issues.push(Issue::Quoted);
    }

    issues
}

/// Same as check(), but also rejects a translation that's just the English string echoed back,
/// which is a common enough AI failure that it's worth catching before we write it to a file
/// (and mark it as translated).
pub fn check_ai_translation(reference: &str, translation: &str) -> Vec<Issue> {
    let mut issues = check(reference, translation);
    if translation.trim() == reference.trim() {
        issues.push(Issue::Untranslated);
    }
    issues
}

fn is_quoted(str: &str) -> bool {
    (str.len() > 1 && str.starts_with('"') && str.ends_with('"')) || str.starts_with("```")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_normal_translations() {
        assert!(check("Graphics", "Grafik").is_empty());
        assert!(check("Slot %1", "Kortplats %1").is_empty());
        assert!(check("%d (%d per core, %d cores)", "%d (%d per kärna, %d kärnor)").is_empty());
        // Reordering placeholders is fine, plenty of languages need to.
        assert!(check("Submitted %1 for %2", "%2 till %1 inskickad").is_empty());
        // Percent signs that aren't placeholders shouldn't bother it.
        assert!(check("Speed: 100%", "Hastighet: 100%").is_empty());
    }

    #[test]
    fn catches_broken_placeholders() {
        // A dropped placeholder, as seen in ko_KR before this was added.
        assert_eq!(
            check("Submitted %1 for %2", "2에 %1을(를) 제출함"),
            vec![Issue::Placeholders {
                expected: "%1 %2".to_string(),
                found: "%1".to_string()
            }]
        );
        // A localized digit, as seen in fa_IR.
        assert!(!check("Quick chat %1", "گپ سریع ۱").is_empty());
        // A space snuck in between the % and the digit, as seen all over km_KH.
        assert!(!check("Slot %1", "រន្ធ% 1").is_empty());
        // Same placeholder, wrong number of them.
        assert!(!check("%d of %d", "%d").is_empty());
    }

    #[test]
    fn catches_bad_ai_output() {
        assert_eq!(check("Graphics", ""), vec![Issue::Empty]);
        assert_eq!(check("Graphics", "  "), vec![Issue::Empty]);
        assert_eq!(check("Graphics", "\"Grafik\""), vec![Issue::Quoted]);
        assert_eq!(check("Graphics", "```Grafik```"), vec![Issue::Quoted]);
        assert_eq!(check("Graphics", "Grafik\nGrafik"), vec![Issue::Newline]);
        assert_eq!(
            check_ai_translation("Graphics", "Graphics"),
            vec![Issue::Untranslated]
        );
        // But check() itself allows it, lots of strings are the same in both languages.
        assert!(check("Graphics", "Graphics").is_empty());
    }
}
