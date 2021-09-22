use std::io;

mod section;

mod inifile;
use inifile::IniFile;

fn copy_missing_lines(reference_ini: &IniFile, target_ini: &mut IniFile) -> io::Result<()> {
    // Insert any missing full sections.
    for reference_section in &reference_ini.sections {
        if !target_ini.insert_section_if_missing(reference_section) {
            if let Some(target_section) = target_ini.get_section_mut(&reference_section.name) {
                for line in &reference_section.lines {
                    target_section.insert_line_if_missing(line);
                }

                //target_section.remove_lines_if_not_in(reference_section);
                target_section.comment_out_lines_if_not_in(reference_section);
            }
        }
    }

    target_ini.write()?;
    Ok(())
}

fn main() {
    let args: Vec<_> = std::env::args().skip(1).collect();

    let mut filenames = args;

    println!("{:?}", filenames);

    let root = "../../assets/lang";
    let reference_file = "en_US.ini";

    let reference_ini = IniFile::parse(&format!("{}/{}", root, reference_file)).unwrap();

    if filenames.is_empty() {
        // Grab them all.
        for path in std::fs::read_dir(root).unwrap() {
            let path = path.unwrap();
            if path.file_name() == reference_file {
                continue;
            }
            filenames.push(path.file_name().to_string_lossy().to_string());
        }
    }

    for filename in filenames {
        if filename == "langtool" || !filename.ends_with("ini") {
            // Get this from cargo run for some reason.
            continue;
        }
        let target_ini_filename = format!("{}/{}", root, filename);
        println!("Langtool processing {}", target_ini_filename);
        let mut target_ini = IniFile::parse(&target_ini_filename).unwrap();
        copy_missing_lines(&reference_ini, &mut target_ini).unwrap();
    }

    // println!("{:#?}", target_ini);
}
