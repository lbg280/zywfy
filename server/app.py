import base64
import hashlib
import json
import os
import time
from typing import Dict

import httpx
from dotenv import load_dotenv
from fastapi import FastAPI, File, HTTPException, UploadFile
from fastapi.responses import JSONResponse

load_dotenv()

DEEPSEEK_API_KEY = os.getenv("DEEPSEEK_API_KEY", "")
DEEPSEEK_MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-chat")
BAIDU_API_KEY = os.getenv("BAIDU_API_KEY", "")
BAIDU_SECRET_KEY = os.getenv("BAIDU_SECRET_KEY", "")

if not all([DEEPSEEK_API_KEY, BAIDU_API_KEY, BAIDU_SECRET_KEY]):
    print("[WARN] Missing API keys. Please configure .env")

app = FastAPI(title="ESP32 Chinese->English Translator")


class TokenCache:
    def __init__(self) -> None:
        self.token = ""
        self.expires_at = 0

    async def get_token(self) -> str:
        now = int(time.time())
        if self.token and now < self.expires_at - 60:
            return self.token

        url = "https://aip.baidubce.com/oauth/2.0/token"
        params = {
            "grant_type": "client_credentials",
            "client_id": BAIDU_API_KEY,
            "client_secret": BAIDU_SECRET_KEY,
        }
        async with httpx.AsyncClient(timeout=15) as client:
            resp = await client.post(url, params=params)
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"Baidu token error: {resp.text}")
        data = resp.json()
        if "access_token" not in data:
            raise HTTPException(status_code=502, detail=f"Baidu token missing: {data}")

        self.token = data["access_token"]
        self.expires_at = now + int(data.get("expires_in", 0))
        return self.token


token_cache = TokenCache()


async def baidu_asr(wav_bytes: bytes) -> str:
    token = await token_cache.get_token()
    speech_b64 = base64.b64encode(wav_bytes).decode("utf-8")
    payload: Dict[str, object] = {
        "format": "wav",
        "rate": 16000,
        "channel": 1,
        "cuid": "esp32-c3-translator",
        "token": token,
        "speech": speech_b64,
        "len": len(wav_bytes),
        "dev_pid": 1537,
    }
    async with httpx.AsyncClient(timeout=30) as client:
        resp = await client.post("https://vop.baidu.com/server_api", json=payload)
    if resp.status_code != 200:
        raise HTTPException(status_code=502, detail=f"ASR request failed: {resp.text}")

    data = resp.json()
    if data.get("err_no") != 0:
        raise HTTPException(status_code=502, detail=f"ASR error: {data}")
    result = data.get("result", [])
    if not result:
        raise HTTPException(status_code=400, detail="ASR result empty")
    return result[0].strip()


async def deepseek_translate_zh_to_en(text: str) -> str:
    headers = {
        "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
        "Content-Type": "application/json",
    }
    body = {
        "model": DEEPSEEK_MODEL,
        "messages": [
            {
                "role": "system",
                "content": "You are a translation assistant. Translate Chinese to natural English only.",
            },
            {"role": "user", "content": text},
        ],
        "temperature": 0.1,
    }
    async with httpx.AsyncClient(timeout=30) as client:
        resp = await client.post("https://api.deepseek.com/chat/completions", headers=headers, json=body)
    if resp.status_code != 200:
        raise HTTPException(status_code=502, detail=f"DeepSeek error: {resp.text}")
    data = resp.json()
    try:
        return data["choices"][0]["message"]["content"].strip()
    except Exception as exc:
        raise HTTPException(status_code=502, detail=f"DeepSeek response parse failed: {data}") from exc


async def baidu_tts(text: str) -> bytes:
    token = await token_cache.get_token()
    params = {
        "tex": text,
        "tok": token,
        "cuid": "esp32-c3-translator",
        "ctp": 1,
        "lan": "zh",
        "spd": 5,
        "pit": 5,
        "vol": 5,
        "per": 0,
        "aue": 6,
    }
    async with httpx.AsyncClient(timeout=30) as client:
        resp = await client.post("https://tsn.baidu.com/text2audio", data=params)

    content_type = resp.headers.get("content-type", "")
    if "audio" not in content_type:
        raise HTTPException(status_code=502, detail=f"TTS error: {resp.text}")
    return resp.content


@app.post("/translate_audio")
async def translate_audio(file: UploadFile = File(...)):
    wav_bytes = await file.read()
    if not wav_bytes:
        raise HTTPException(status_code=400, detail="empty audio")

    zh_text = await baidu_asr(wav_bytes)
    en_text = await deepseek_translate_zh_to_en(zh_text)
    tts_mp3 = await baidu_tts(en_text)

    tts_b64 = base64.b64encode(tts_mp3).decode("utf-8")
    checksum = hashlib.md5(tts_mp3).hexdigest()

    return JSONResponse(
        {
            "zh_text": zh_text,
            "en_text": en_text,
            "tts_audio_base64": tts_b64,
            "tts_md5": checksum,
            "tts_format": "mp3",
        }
    )


@app.get("/health")
async def health():
    return {"ok": True}
