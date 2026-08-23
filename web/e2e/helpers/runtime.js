export async function restartWorkbench(page) {
  await page.evaluate(() => { globalThis.__picoTrackerWorkbench.restart() })
}

export async function stopWorkbench(page) {
  await page.evaluate(() => globalThis.__picoTrackerWorkbench.stop())
}
