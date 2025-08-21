use anyhow::Context;
use serde::{Deserialize, Serialize};
use std::{error::Error, time::Duration};

#[derive(Serialize)]
pub struct ChatRequest<'a> {
    pub model: &'a str,
    pub messages: Vec<Message<'a>>,
}

#[derive(Serialize)]
pub struct Message<'a> {
    pub role: &'a str,
    pub content: &'a str,
}

#[derive(Deserialize)]
pub struct ChatResponse {
    pub choices: Vec<Choice>,
}

#[derive(Deserialize)]
pub struct Choice {
    pub message: MessageResponse,
}

#[allow(dead_code)]
#[derive(Deserialize)]
pub struct MessageResponse {
    pub content: String,
    pub role: String, // unused
}

pub struct ChatGPT {
    api_key: String,
    model: String,
}

impl ChatGPT {
    pub fn new(api_key: String, model: String) -> Self {
        ChatGPT { api_key, model }
    }

    pub fn chat(&self, request: &str) -> Result<String, Box<dyn Error>> {
        let client = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(300))
            .build()?;

        let req_body = ChatRequest {
            model: &self.model,
            messages: vec![Message {
                role: "user",
                content: request,
            }],
        };

        let res: ChatResponse = client
            .post("https://api.openai.com/v1/chat/completions")
            .bearer_auth(&self.api_key)
            .json(&req_body)
            .send()
            .context("response")?
            .json()
            .context("json")?;

        Ok(res.choices[0].message.content.clone())
    }
}
