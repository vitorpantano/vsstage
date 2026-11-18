# Arquitetura (Windows)

- **Motor** (`engine/`): `MultitrackEngine::render()` é chamado direto do callback do miniaudio. Sem alocação nem locks no thread de áudio; controle por atômicos e filas. Mesmo código do Android.
- **AudioDevice** (`core/Audio.*`): contexto miniaudio (WASAPI), `f32`, N canais, `ma_performance_profile_low_latency`, tentativa de modo exclusivo com queda automática para compartilhado e de N canais para estéreo. Mede CPU do callback e conta estouros de tempo.
- **Player** (`core/Player.*`): espelha o `MultitrackPlayer` do Android. Decodifica faixas em threads, roda `detectTempo` na 1ª carga e entrega os resultados à thread da UI em `update()` (sem tocar no motor de outras threads).
- **Library** (`core/Library.*`): `library.json` (escrita atômica) + `songs/<id>/` com os áudios copiados. Pacotes `.vsstage` = zip (miniz) com `manifest.json` v1 e `audio/<ref>/<arquivo>`, o mesmo formato do Android.
- **Mapper** (`core/Mapper.*`): porte do `SmartMapper` (nome do arquivo → papel/título/BPM/tom).
- **MIDI** (`core/Midi.*`): RtMidi em thread própria só enfileira; a UI consome em `poll()`.
- **UI** (`src/`): modo imediato, tudo na thread principal. Ícones desenhados (não dependem de glifos da fonte); fonte do sistema (Segoe UI).
