#pragma once

// Enters the temporary ST7789 gamma comparison screen when physical PLAY and
// SHIFT are held during boot. Returns immediately during a normal boot.
void RunDisplayGammaCalibrationIfRequested();
