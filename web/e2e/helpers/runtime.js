export async function restartWorkbench(page) {
  // Await the whole stop/start operation, including failures. Fire-and-forget
  // can lose a fast stopping edge or turn a rejected restart into a timeout.
  await page.evaluate(() => globalThis.__picoTrackerWorkbench.restart())
}

export async function stopWorkbench(page) {
  await page.evaluate(() => globalThis.__picoTrackerWorkbench.stop())
}
