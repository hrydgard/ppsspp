// Super simplified ini file processor.
// Doesn't even bother with understanding comments.
// Just understands section headings and
// keys and values, split by ' = '.

#[derive(Debug, Clone)]
pub struct Section {
    pub name: String,
    pub title_line: String,
    pub lines: Vec<String>,
}

impl Section {
    pub fn remove_line(&mut self, key: &str) -> Option<String> {
        let mut remove_index = None;
        for (index, line) in self.lines.iter().enumerate() {
            let prefix = if let Some(pos) = line.find(" =") {
                &line[0..pos]
            } else {
                continue;
            };

            if prefix.eq_ignore_ascii_case(key) {
                remove_index = Some(index);
                break;
            }
        }

        if let Some(remove_index) = remove_index {
            Some(self.lines.remove(remove_index))
        } else {
            None
        }
    }

    pub fn get_line(&mut self, key: &str) -> Option<String> {
        for line in self.lines.iter() {
            let prefix = if let Some(pos) = line.find(" =") {
                &line[0..pos]
            } else {
                continue;
            };

            if prefix.eq_ignore_ascii_case(key) {
                return Some(line.clone());
            }
        }
        None
    }

    pub fn insert_line_if_missing(&mut self, line: &str) -> bool {
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

    pub fn rename_key(&mut self, old: &str, new: &str) {
        let prefix = old.to_owned() + " =";
        let mut found_index = None;
        for (index, line) in self.lines.iter().enumerate() {
            if line.starts_with(&prefix) {
                found_index = Some(index);
            }
        }
        if let Some(index) = found_index {
            let line = self.lines.remove(index);
            let mut right_part = line.strip_prefix(&prefix).unwrap().to_string();
            if right_part.trim() == old.trim() {
                // Was still untranslated - replace the translation too.
                right_part = format!(" {}", new);
            }
            let line = new.to_owned() + " =" + &right_part;
            self.insert_line_if_missing(&line);
        } else {
            let name = &self.name;
            println!("didn't find a line starting with {prefix} in section {name}");
        }
    }

    pub fn sort(&mut self) {
        self.lines.sort();
    }

    pub fn comment_out_lines_if_not_in(&mut self, other: &Section) {
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
            if !other.lines.iter().any(|line| line.starts_with(prefix)) && !prefix.contains("URL") {
                println!("Commenting out from {}: {line}", other.name);
                // Comment out the line.
                *line = "#".to_owned() + line;
            }
        }
    }

    pub fn remove_lines_if_not_in(&mut self, other: &Section) {
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

            // keeps the line if this expression returns true.
            other.lines.iter().any(|line| line.starts_with(prefix))
        });
    }
}
