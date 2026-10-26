// Finds translation lookups in the C++ source whose key isn't in en_US.ini.
//
// This is a regex scan, not a parser, so it knows the lookup patterns we use:
// - `xx->T("Key")` / `T_cstr`, where xx comes from `GetI18NCategory(I18NCat::...)` in the same file.
// - `T(I18NCat::X, "Key")` and `{ I18NCat::X, "Key" }` (SettingText and friends).
// - Config labels: `.Label(I18NCat::X, "Key")` plus the chained `.Choices(array)`, `.ZeroLabel()`,
//   `.NegativeLabel()` and `.Units()`.
// - `new PopupMultiChoice(..., array, ..., I18NCat::X, ...)` and `ContextMenuItem` arrays.
// - Bare strings on SettingsPage calls (`page->Header("Key")`), which use the page's category. That's
//   found from the page table, `page->Link(..., I18NCat::X, &BuildFoo)` and the developer tools links.
//
// Keys built at runtime can't be checked, so `dynamic` lists those lookups instead.

use std::collections::{BTreeMap, HashMap};
use std::fs;
use std::path::{Path, PathBuf};

use regex::Regex;

use crate::inifile::IniFile;

const SOURCE_DIRS: &[&str] = &[
    "UI",
    "Common",
    "Core",
    "GPU",
    "Windows",
    "SDL",
    "android/jni",
    "ios",
    "UWP",
    "libretro",
];

// Build output and third party code.
const SKIP_DIRS: &[&str] = &["x64", "ARM64", "Win32", "Debug", "Release", "ext"];

// Must match g_categoryNames in Common/Data/Text/I18n.cpp.
const CATEGORIES: &[(&str, &str)] = &[
    ("AUDIO", "Audio"),
    ("CONTROLS", "Controls"),
    ("CWCHEATS", "CwCheats"),
    ("DESKTOPUI", "DesktopUI"),
    ("DEVELOPER", "Developer"),
    ("DIALOG", "Dialog"),
    ("ERRORS", "Error"),
    ("GAME", "Game"),
    ("GRAPHICS", "Graphics"),
    ("INSTALLZIP", "InstallZip"),
    ("KEYMAPPING", "KeyMapping"),
    ("MAINMENU", "MainMenu"),
    ("MAINSETTINGS", "MainSettings"),
    ("MAPPABLECONTROLS", "MappableControls"),
    ("NETWORKING", "Networking"),
    ("PAUSE", "Pause"),
    ("POSTSHADERS", "PostShaders"),
    ("PSPCREDITS", "PSPCredits"),
    ("MEMSTICK", "MemStick"),
    ("REMOTEISO", "RemoteISO"),
    ("REPORTING", "Reporting"),
    ("SAVEDATA", "Savedata"),
    ("SCREEN", "Screen"),
    ("SEARCH", "Search"),
    ("STORE", "Store"),
    ("SYSINFO", "SysInfo"),
    ("SYSTEM", "System"),
    ("TEXTURESHADERS", "TextureShaders"),
    ("THEMES", "Themes"),
    ("UI_ELEMENTS", "UI Elements"),
    ("VR", "VR"),
    ("ACHIEVEMENTS", "Achievements"),
    ("PSPSETTINGS", "PSPSettings"),
];

// A C++ string literal, capturing its contents.
const STR: &str = r#""((?:[^"\\]|\\.)*)""#;

fn section_name(enum_name: &str) -> Option<&'static str> {
    CATEGORIES
        .iter()
        .find(|(e, _)| *e == enum_name)
        .map(|(_, s)| *s)
}

// Blanks out comments, keeping offsets (and so line numbers) unchanged.
fn strip_comments(src: &str) -> String {
    let b = src.as_bytes();
    let mut out = b.to_vec();
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            q @ (b'"' | b'\'') => {
                i += 1;
                while i < b.len() && b[i] != q && b[i] != b'\n' {
                    if b[i] == b'\\' {
                        i += 1;
                    }
                    i += 1;
                }
                i += 1;
            }
            b'/' if b.get(i + 1) == Some(&b'/') => {
                while i < b.len() && b[i] != b'\n' {
                    out[i] = b' ';
                    i += 1;
                }
            }
            b'/' if b.get(i + 1) == Some(&b'*') => {
                let end = src[i + 2..].find("*/").map_or(b.len(), |p| i + 2 + p + 2);
                while i < end {
                    if b[i] != b'\n' {
                        out[i] = b' ';
                    }
                    i += 1;
                }
            }
            _ => i += 1,
        }
    }
    // Only whole bytes of comments were replaced, with ASCII.
    String::from_utf8_lossy(&out).into_owned()
}

fn collect_files(dir: &Path, files: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().to_string();
        if path.is_dir() {
            if !SKIP_DIRS.contains(&name.as_str()) {
                collect_files(&path, files);
            }
        } else if [".cpp", ".h", ".mm", ".m"]
            .iter()
            .any(|ext| name.ends_with(ext))
        {
            files.push(path);
        }
    }
}

fn unescape(s: &str) -> String {
    // \n stays as is, the ini files spell line breaks the same way.
    s.replace("\\\"", "\"").replace("\\\\", "\\")
}

fn line_number(s: &str, pos: usize) -> usize {
    s[..pos].matches('\n').count() + 1
}

struct SourceFile {
    rel: String,
    text: String,
}

#[derive(Default)]
struct Scanner {
    // (section, key) -> (where, the English default if the lookup has one)
    missing: BTreeMap<(String, String), (String, Option<String>)>,
}

impl Scanner {
    fn check(
        &mut self,
        reference: &IniFile,
        section: Option<&str>,
        key: &str,
        default: Option<&str>,
        loc: String,
    ) {
        let Some(section) = section else {
            return;
        };
        let key = unescape(key);
        // Numbers in choice lists (like frame skip counts) don't need translating. "Font" is an optional
        // per-language font override, which I18NCategory doesn't report as missing either.
        if key.is_empty() || key.chars().all(|c| c.is_ascii_digit()) || key == "Font" {
            return;
        }
        // Not get_value(), which doesn't see keys with empty values (like "translators6 =").
        let found = reference.get_section(section).is_some_and(|s| {
            s.lines
                .iter()
                .any(|line| line.split_once('=').is_some_and(|(k, _)| k.trim() == key))
        });
        if !found {
            self.missing
                .entry((section.to_string(), key))
                .or_insert((loc, default.map(unescape)));
        }
    }
}

// Lists the lookups whose key isn't in en_US.ini, or with `dynamic`, the ones whose key or category
// is only known at runtime. Returns how many it found.
pub fn find_missing_keys(source_root: &str, reference: &IniFile, dynamic: bool) -> usize {
    let mut paths = Vec::new();
    for dir in SOURCE_DIRS {
        collect_files(&Path::new(source_root).join(dir), &mut paths);
    }
    paths.sort();
    let files: Vec<SourceFile> = paths
        .iter()
        .filter_map(|p| {
            let text = fs::read(p).ok()?;
            let rel = p
                .strip_prefix(source_root)
                .unwrap_or(p)
                .to_string_lossy()
                .replace('\\', "/");
            Some(SourceFile {
                rel,
                text: strip_comments(&String::from_utf8_lossy(&text)),
            })
        })
        .collect();

    if dynamic {
        return list_dynamic(&files);
    }

    let re = |s: &str| Regex::new(s).unwrap();
    let array_re =
        re(r"(?s)(?:const\s+)?char\s*\*\s*(?:const\s+)?(\w+)\s*\[\s*\w*\s*\]\s*=\s*\{(.*?)\};");
    let str_re = re(STR);

    // String arrays used as choices, by name.
    let mut arrays: HashMap<String, Vec<String>> = HashMap::new();
    for f in &files {
        for c in array_re.captures_iter(&f.text) {
            let items = str_re
                .captures_iter(&c[2])
                .map(|m| m[1].to_string())
                .collect();
            arrays.entry(c[1].to_string()).or_insert(items);
        }
    }

    // Settings page builders, and the category their bare strings use.
    let table_re = re(r"I18NCat::(\w+)\s*,\s*(?:nullptr\s*,\s*)?&(\w+)\s*\}");
    let link_re = re(r"Link\([^;]*?,\s*I18NCat::(\w+)\s*,\s*&(\w+)\)");
    // BuildDeveloperToolsPage's link lambda, which passes I18NCat::DEVELOPER.
    let dev_link_re = re(r"\blink\(\s*\{[^}]*\}\s*,\s*&(\w+)\)");
    let mut page_cats: HashMap<String, &str> = HashMap::new();
    for f in &files {
        for c in table_re
            .captures_iter(&f.text)
            .chain(link_re.captures_iter(&f.text))
        {
            if let Some(s) = section_name(&c[1]) {
                page_cats.insert(c[2].to_string(), s);
            }
        }
    }
    for f in &files {
        for c in dev_link_re.captures_iter(&f.text) {
            page_cats.entry(c[1].to_string()).or_insert("Developer");
        }
    }

    let assign_re = re(r"(\w+)\s*=\s*GetI18NCategory\(\s*I18NCat::(\w+)\s*\)");
    let var_t_re = re(&format!(
        r"\b(\w+)\s*->\s*T(?:_cstr)?\(\s*{STR}(?:\s*,\s*{STR})?"
    ));
    let free_t_re = re(&format!(
        r"(?:^|[^\w>.])T(?:_cstr)?\(\s*I18NCat::(\w+)\s*,\s*{STR}(?:\s*,\s*{STR})?"
    ));
    let text_re = re(&format!(
        r"[{{(]\s*I18NCat::(\w+)\s*,\s*{STR}(?:\s*,\s*{STR})?\s*[}})]"
    ));
    let label_re = re(&format!(
        r"\.Label\(\s*I18NCat::(\w+)\s*,\s*{STR}(?:\s*,\s*{STR})?\)((?:\s*\.\w+\([^()]*\))*)"
    ));
    let choices_re = re(r"\.Choices\(\s*(\w+)");
    let label_extra_re = re(&format!(r"\.(?:ZeroLabel|NegativeLabel)\(\s*{STR}"));
    let units_re = re(&format!(r"\.Units\(\s*{STR}"));
    let popup_re = re(r"new\s+Popup\w*Choice\w*\(([^;]*?)\)\s*\)");
    let cat_re = re(r"I18NCat::(\w+)");
    let ident_re = re(r"\b(\w+)\b");
    let menu_re = re(r"(?s)ContextMenuItem\s+(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};");
    let menu_item_re = re(&format!(r"\{{\s*{STR}"));
    let page_fn_re = re(r"\bvoid\s+(\w+)\s*\(\s*SettingsPage\s*\*\s*page\s*\)\s*\{");
    let page_call_re = re(&format!(
        r"\bpage->(?:Header|Choice|Hint|Custom|Info|Link|Bool)\(\s*{STR}"
    ));
    let page_call2_re = re(&format!(r#"\bpage->(?:Bool|IntSlider)\([^;"]*?,\s*{STR}"#));

    let mut scanner = Scanner::default();
    for f in &files {
        let s = &f.text;
        let loc = |pos: usize| format!("{}:{}", f.rel, line_number(s, pos));

        // A variable's category is the one it was last assigned before the use.
        let assigns: Vec<(usize, String, Option<&str>)> = assign_re
            .captures_iter(s)
            .map(|c| {
                (
                    c.get(0).unwrap().start(),
                    c[1].to_string(),
                    section_name(&c[2]),
                )
            })
            .collect();
        let var_cat = |var: &str, pos: usize| -> Option<&str> {
            let matching = assigns.iter().filter(|(_, v, _)| v == var);
            let first = matching.clone().next().and_then(|a| a.2);
            matching
                .filter(|(p, _, _)| *p < pos)
                .last()
                .map_or(first, |a| a.2)
        };

        for c in var_t_re.captures_iter(s) {
            let pos = c.get(0).unwrap().start();
            let cat = var_cat(&c[1], pos);
            scanner.check(
                reference,
                cat,
                &c[2],
                c.get(3).map(|m| m.as_str()),
                loc(pos),
            );
        }
        for re in [&free_t_re, &text_re] {
            for c in re.captures_iter(s) {
                let pos = c.get(0).unwrap().start();
                let cat = section_name(&c[1]);
                scanner.check(
                    reference,
                    cat,
                    &c[2],
                    c.get(3).map(|m| m.as_str()),
                    loc(pos),
                );
            }
        }

        for c in label_re.captures_iter(s) {
            let pos = c.get(0).unwrap().start();
            let cat = section_name(&c[1]);
            scanner.check(
                reference,
                cat,
                &c[2],
                c.get(3).map(|m| m.as_str()),
                loc(pos),
            );
            let chain = &c[4];
            for cm in choices_re.captures_iter(chain) {
                for k in arrays.get(&cm[1]).into_iter().flatten() {
                    scanner.check(
                        reference,
                        cat,
                        k,
                        None,
                        format!("{} (choices {})", loc(pos), &cm[1]),
                    );
                }
            }
            for cm in label_extra_re.captures_iter(chain) {
                scanner.check(reference, cat, &cm[1], None, loc(pos));
            }
            // Short units like "ms" and "%" are shown as is, see TranslateUnits.
            for cm in units_re.captures_iter(chain) {
                let u = &cm[1];
                if u.len() > 3 && u.chars().any(|c| c.is_ascii_alphabetic()) {
                    scanner.check(reference, cat, u, None, format!("{} (units)", loc(pos)));
                }
            }
        }

        for c in popup_re.captures_iter(s) {
            let args = &c[1];
            let Some(cm) = cat_re.captures(args) else {
                continue;
            };
            let cat = section_name(&cm[1]);
            let pos = c.get(0).unwrap().start();
            // Identifiers only, not words inside string arguments.
            let idents = str_re.replace_all(args, "\"\"");
            for im in ident_re.captures_iter(&idents) {
                if let Some(items) = arrays.get(&im[1]) {
                    for k in items {
                        scanner.check(
                            reference,
                            cat,
                            k,
                            None,
                            format!("{} (choices {})", loc(pos), &im[1]),
                        );
                    }
                }
            }
        }

        for c in menu_re.captures_iter(s) {
            let use_re = Regex::new(&format!(
                r"PopupContextMenuScreen\(\s*{}\s*,[^,]*,\s*I18NCat::(\w+)",
                regex::escape(&c[1])
            ))
            .unwrap();
            let Some(um) = use_re.captures(s) else {
                eprintln!("No category found for the menu {} in {}", &c[1], f.rel);
                continue;
            };
            let cat = section_name(&um[1]);
            let pos = c.get(0).unwrap().start();
            for im in menu_item_re.captures_iter(&c[2]) {
                scanner.check(
                    reference,
                    cat,
                    &im[1],
                    None,
                    format!("{} (menu {})", loc(pos), &c[1]),
                );
            }
        }

        for fm in page_fn_re.captures_iter(s) {
            let name = &fm[1];
            let start = fm.get(0).unwrap().end();
            // The function body, up to the matching brace.
            let mut depth = 1;
            let mut end = start;
            for (i, ch) in s[start..].char_indices() {
                match ch {
                    '{' => depth += 1,
                    '}' => {
                        depth -= 1;
                        if depth == 0 {
                            end = start + i;
                            break;
                        }
                    }
                    _ => {}
                }
            }
            let Some(&cat) = page_cats.get(name) else {
                eprintln!("No page category found for {} in {}", name, f.rel);
                continue;
            };
            let body = &s[start..end];
            for re in [&page_call_re, &page_call2_re] {
                for c in re.captures_iter(body) {
                    let pos = start + c.get(0).unwrap().start();
                    scanner.check(reference, Some(cat), &c[1], None, loc(pos));
                }
            }
        }
    }

    for ((section, key), (loc, default)) in &scanner.missing {
        match default {
            Some(d) => println!("[{section}] {key} = {d}  ({loc})"),
            None => println!("[{section}] {key}  ({loc})"),
        }
    }
    println!("{} missing keys", scanner.missing.len());
    scanner.missing.len()
}

// Lookups whose key (or category) is a variable or expression rather than a string literal.
fn list_dynamic(files: &[SourceFile]) -> usize {
    let var_t_re = Regex::new(r#"\b(\w+)\s*->\s*T(?:_cstr)?\(\s*([^"\s)][^,;]*?)\s*[,)]"#).unwrap();
    let free_t_re = Regex::new(
        r#"(?:^|[^\w>.:])T(?:_cstr)?\(\s*(I18NCat::\w+|[a-z]\w*)\s*,\s*([^,;]*?)\s*[,)]"#,
    )
    .unwrap();
    let mut count = 0;
    for f in files {
        // The lookup functions themselves.
        if f.rel.starts_with("Common/Data/Text/I18n.") {
            continue;
        }
        let s = &f.text;
        let mut found: Vec<(usize, String)> = Vec::new();
        for c in var_t_re.captures_iter(s) {
            found.push((
                c.get(0).unwrap().start(),
                format!("{}->T({})", &c[1], &c[2]),
            ));
        }
        for c in free_t_re.captures_iter(s) {
            let (cat, key) = (&c[1], &c[2]);
            if cat.starts_with("I18NCat::") && key.starts_with('"') {
                continue;
            }
            found.push((c.get(1).unwrap().start(), format!("T({cat}, {key})")));
        }
        found.sort();
        for (pos, what) in found {
            println!("{}:{}: {}", f.rel, line_number(s, pos), what);
            count += 1;
        }
    }
    println!("{count} lookups with a runtime key or category");
    count
}
