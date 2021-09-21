use std::fs::File;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};

// Super simplified ini file processor.
// Doesn't even bother with understanding comments.
// Just understands section headings and
// keys and values, split by ' = '.

#[derive(Debug, Clone)]
struct Section {
    name: String,
    title_line: String,
    lines: Vec<String>,
}

impl Section {
    fn insert_line_if_missing(&mut self, line: &str) -> bool {
        let prefix = if let Some(pos) = line.find(" =") {
            &line[0..pos + 2]
        } else {
            return false;
        };
        // Ignore comments when copying lines.
        if prefix.starts_with('#') {
            return false;
        }
        // Need to decide a policy for these.
        if prefix.starts_with("translators") {
            return false;
        }
        let prefix = prefix.to_owned();

        for iter_line in &self.lines {
            if iter_line.starts_with(&prefix) {
                // Already have it
                return false;
            }
        }

        // Now try to insert it at an alphabetic-ish location.
        let prefix = prefix.to_ascii_lowercase();

        // Then, find a suitable insertion spot
        for (i, iter_line) in self.lines.iter().enumerate() {
            if iter_line.to_ascii_lowercase() > prefix {
                println!("Inserting line {} into {}", line, self.name);
                self.lines.insert(i, line.to_owned());
                return true;
            }
        }

        for i in (0..self.lines.len()).rev() {
            if self.lines[i].is_empty() {
                continue;
            }
            println!("Inserting line {} into {}", line, self.name);
            self.lines.insert(i + 1, line.to_owned());
            return true;
        }

        println!("failed to insert {}", line);
        true
    }

    fn comment_out_lines_if_not_in(&mut self, other: &Section) {
        // Brute force (O(n^2)). Bad but not a problem.

        for line in &mut self.lines {
            let prefix = if let Some(pos) = line.find(" =") {
                &line[0..pos + 2]
            } else {
                // Keep non-key lines.
                continue;
            };
            if prefix.starts_with("Font") || prefix.starts_with('#') {
                continue;
            }
            if !other.lines.iter().any(|line| line.starts_with(prefix)) {
                println!("Commenting out: {}", line);
                // Comment out the line.
                *line = "#".to_owned() + line;
            }
        }
    }

    fn remove_lines_if_not_in(&mut self, other: &Section) {
        // Brute force (O(n^2)). Bad but not a problem.

        self.lines.retain(|line| {
            let prefix = if let Some(pos) = line.find(" =") {
                &line[0..pos + 2]
            } else {
                // Keep non-key lines.
                return true;
            };
            if prefix.starts_with("Font") || prefix.starts_with('#') {
                return true;
            }
            if !other.lines.iter().any(|line| line.starts_with(prefix)) {
                false
            } else {
                true
            }
        });
    }
}

#[derive(Debug)]
struct IniFile {
    filename: PathBuf,
    preamble: Vec<String>,
    sections: Vec<Section>,
    has_bom: bool,
}

impl IniFile {
    fn parse(filename: &str) -> io::Result<IniFile> {
        let lines = read_lines(filename)?;

        let mut sections = vec![];
        let mut preamble = vec![];
        let mut cur_section = None;

        let mut has_bom = false;

        for line in lines {
            let line = line.unwrap();

            let line = if let Some(line) = line.strip_prefix('\u{feff}') {
                has_bom = true;
                line
            } else {
                &line
            };

            if let Some('[') = line.chars().next() {
                if let Some(right_bracket) = line.find(']') {
                    if let Some(section) = cur_section.take() {
                        sections.push(section);
                    }

                    let name = &line[1..right_bracket];
                    cur_section = Some(Section {
                        name: name.to_owned(),
                        title_line: line.to_owned(), // preserves comment and bom
                        lines: vec![],
                    });
                } else {
                    // Bad syntax
                    break;
                }
            } else if let Some(cur_section) = &mut cur_section {
                cur_section.lines.push(line.to_owned());
            } else {
                preamble.push(line.to_owned());
            }
        }

        if let Some(section) = cur_section.take() {
            sections.push(section);
        }

        let ini = IniFile {
            filename: PathBuf::from(filename),
            preamble,
            sections,
            has_bom,
        };
        Ok(ini)
    }

    fn write(&self) -> io::Result<()> {
        let file = std::fs::File::create(&self.filename)?;
        let mut file = std::io::LineWriter::new(file);

        // Write BOM
        if self.has_bom {
            file.write_all("\u{feff}".as_bytes())?;
        }
        for line in &self.preamble {
            file.write_all(line.as_bytes())?;
            file.write_all(b"\n")?;
        }
        for section in &self.sections {
            file.write_all(section.title_line.as_bytes())?;
            file.write_all(b"\n")?;
            for line in &section.lines {
                file.write_all(line.as_bytes())?;
                file.write_all(b"\n")?;
            }
        }

        Ok(())
    }

    // Assumes alphabetical section order!
    fn insert_section_if_missing(&mut self, section: &Section) -> bool {
        // First, check if it's there.

        for iter_section in &self.sections {
            if iter_section.name == section.name {
                return false;
            }
        }

        // Then, find a suitable insertion spot
        for (i, iter_section) in self.sections.iter_mut().enumerate() {
            if iter_section.name > section.name {
                println!("Inserting section {}", section.name);
                self.sections.insert(i, section.clone());
                return true;
            }
        }
        // Reached the end for some reason? Add it.
        self.sections.push(section.clone());
        true
    }

    fn get_section_mut(&mut self, section_name: &str) -> Option<&mut Section> {
        for section in &mut self.sections {
            if section.name == section_name {
                return Some(section);
            }
        }
        None
    }
}

// Grabbed from a sample, a fast line reader iterator.
fn read_lines<P>(filename: P) -> io::Result<io::Lines<io::BufReader<File>>>
where
    P: AsRef<Path>,
{
    let file = File::open(filename)?;
    Ok(io::BufReader::new(file).lines())
}

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
