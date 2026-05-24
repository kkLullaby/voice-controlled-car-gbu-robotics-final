"""DashScope streaming ASR listener for microphone input."""

from __future__ import annotations

import queue
from typing import Iterator, Protocol


class StopFlag(Protocol):
    """Minimal stop flag interface for web/background ASR workers."""

    def is_set(self) -> bool: ...


class DashScopeMicListener:
    """Stream local microphone audio to DashScope and yield recognized text."""

    def __init__(
        self,
        api_key: str,
        *,
        model: str = "paraformer-realtime-v2",
        sample_rate: int = 16000,
        channels: int = 1,
        chunk_size: int = 3200,
    ):
        self.api_key = api_key
        self.model = model
        self.sample_rate = sample_rate
        self.channels = channels
        self.chunk_size = chunk_size
        self._texts: queue.Queue[str] = queue.Queue()

    def listen(self, stop_flag: StopFlag | None = None) -> Iterator[str]:
        """Yield ASR transcripts until interrupted by Ctrl+C."""

        if not self.api_key.strip():
            raise RuntimeError("DASHSCOPE_API_KEY is required for ASR mode")

        try:
            import dashscope
            import pyaudio
            from dashscope.audio.asr import Recognition, RecognitionCallback, RecognitionResult
        except ImportError as exc:
            raise RuntimeError(
                "ASR dependencies are missing. Run: pip install -r requirements.txt"
            ) from exc

        dashscope.api_key = self.api_key
        texts = self._texts

        class Callback(RecognitionCallback):
            def on_open(self):
                print("[asr] connected to DashScope")

            def on_error(self, result: RecognitionResult):
                print(f"[asr] error: {result.message}")

            def on_event(self, result: RecognitionResult):
                sentence = result.get_sentence()
                text = sentence.get("text", "").strip() if sentence else ""
                if text:
                    texts.put(text)

        audio = pyaudio.PyAudio()
        stream = audio.open(
            format=pyaudio.paInt16,
            channels=self.channels,
            rate=self.sample_rate,
            input=True,
            frames_per_buffer=self.chunk_size,
        )
        recognition = Recognition(
            model=self.model,
            format="pcm",
            sample_rate=self.sample_rate,
            callback=Callback(),
        )

        recognition.start()
        print("[asr] listening, press Ctrl+C to stop")
        try:
            while stop_flag is None or not stop_flag.is_set():
                data = stream.read(self.chunk_size, exception_on_overflow=False)
                recognition.send_audio_frame(data)
                yield from self._drain_texts()
            yield from self._drain_texts()
        finally:
            recognition.stop()
            stream.stop_stream()
            stream.close()
            audio.terminate()

    def _drain_texts(self) -> Iterator[str]:
        while True:
            try:
                yield self._texts.get_nowait()
            except queue.Empty:
                return
