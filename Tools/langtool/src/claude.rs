use anyhow::{Context, anyhow};
use serde::{Deserialize, Serialize};
use std::{error::Error, time::Duration};

// The Anthropic API version header. Not the model version, this basically never changes.
const API_VERSION: &str = "2023-06-01";

// Plenty for a section's worth of translated strings. If we ever hit it, we get a
// "max_tokens" stop reason and complain loudly instead of silently truncating.
const MAX_TOKENS: u32 = 16000;

#[derive(Serialize)]
pub struct MessagesRequest<'a> {
    pub model: &'a str,
    pub max_tokens: u32,
    pub messages: Vec<Message<'a>>,
}

#[derive(Serialize)]
pub struct Message<'a> {
    pub role: &'a str,
    pub content: &'a str,
}

#[derive(Deserialize)]
pub struct MessagesResponse {
    pub content: Vec<ContentBlock>,
    pub stop_reason: Option<String>,
}

// Content comes back as a list of blocks. We only care about the text ones, but there can
// be others (thinking blocks, tool use), so we can't just assume block 0 is text.
#[derive(Deserialize)]
pub struct ContentBlock {
    #[serde(rename = "type")]
    pub block_type: String,
    #[serde(default)]
    pub text: Option<String>,
}

pub struct Claude {
    api_key: String,
    model: String,
}

impl Claude {
    pub fn new(api_key: String, model: String) -> Self {
        Claude { api_key, model }
    }

    pub fn model(&self) -> &str {
        &self.model
    }

    pub fn chat(&self, request: &str) -> Result<String, Box<dyn Error>> {
        let client = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(300))
            .build()?;

        let req_body = MessagesRequest {
            model: &self.model,
            max_tokens: MAX_TOKENS,
            messages: vec![Message {
                role: "user",
                content: request,
            }],
        };

        let res = client
            .post("https://api.anthropic.com/v1/messages")
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", API_VERSION)
            .json(&req_body)
            .send()
            .context("response")?;

        // Errors come back as a JSON body with a useful message in it, so print it as-is
        // rather than just the status code.
        let status = res.status();
        if !status.is_success() {
            let body = res.text().unwrap_or_default();
            return Err(anyhow!("Claude API error ({status}): {body}").into());
        }

        let res: MessagesResponse = res.json().context("json")?;

        match res.stop_reason.as_deref() {
            Some("max_tokens") => {
                return Err(
                    anyhow!("Response hit the {MAX_TOKENS} token limit, it got cut off").into(),
                );
            }
            Some("refusal") => {
                return Err(anyhow!("Claude declined to answer this request").into());
            }
            _ => {}
        }

        let text = res
            .content
            .iter()
            .filter(|block| block.block_type == "text")
            .filter_map(|block| block.text.as_deref())
            .collect::<Vec<_>>()
            .join("");

        if text.is_empty() {
            return Err(anyhow!("No text in the response from Claude").into());
        }

        Ok(text)
    }
}
