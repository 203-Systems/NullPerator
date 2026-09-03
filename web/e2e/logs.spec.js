import { expect, test } from '@playwright/test'
import { readFile } from 'node:fs/promises'
import { stopWorkbench } from './helpers/runtime.js'

test('bounded Logs panel filters, pauses, resumes, clears, copies, and downloads JSONL', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write'], {
    origin: 'http://127.0.0.1:4173',
  })
  await page.goto('/?logs-test=1&dev=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.evaluate(() => {
    globalThis.__picoTrackerLogsTest.append({
      monotonicUs: 10, wallTime: 1_700_000_000_000, severity: 'info',
      category: 'ACCEPTANCE', thread: 'browser', message: 'visible needle record',
    })
    globalThis.__picoTrackerLogsTest.append({
      monotonicUs: 20, wallTime: 1_700_000_000_001, severity: 'error',
      category: 'EXPORT', thread: 'browser', message: 'download record',
    })
    globalThis.__picoTrackerLogsTest.append({
      monotonicUs: 30, wallTime: 1_700_000_000_002, severity: 'info',
      category: 'PERSISTENCYDOCUMENT', thread: 'application', message: 'long category layout record',
    })
  })
  await page.getByRole('button', { name: 'Logs', exact: true }).click()
  const panel = page.locator('.logs-panel')
  const needle = panel.getByText('visible needle record', { exact: true })
  const exported = panel.getByText('download record', { exact: true })
  await expect(needle).toBeVisible()

  await page.setViewportSize({ width: 390, height: 844 })
  const longCategoryRow = panel.getByText('long category layout record', { exact: true }).locator('..')
  await expect(longCategoryRow).toBeVisible()
  expect(await longCategoryRow.evaluate((element) => element.scrollWidth <= element.clientWidth)).toBe(true)
  await page.setViewportSize({ width: 1280, height: 720 })

  await panel.getByLabel('Severity').selectOption('error')
  await expect(exported).toBeVisible()
  await expect(needle).not.toBeVisible()
  await panel.getByLabel('Severity').selectOption('debug')

  await panel.getByLabel('Category').selectOption('ACCEPTANCE')
  await expect(needle).toBeVisible()
  await expect(exported).not.toBeVisible()
  await panel.getByLabel('Category').selectOption('')

  await panel.getByLabel('Search').fill('VISIBLE NEEDLE')
  await expect(needle).toBeVisible()
  await expect(exported).not.toBeVisible()

  // Copy and download both export the current combined filter, not the
  // complete retained ring.
  await panel.getByLabel('Severity').selectOption('error')
  await panel.getByLabel('Category').selectOption('EXPORT')
  await panel.getByLabel('Search').fill('DOWNLOAD')
  await panel.getByRole('button', { name: 'Copy', exact: true }).click()
  await expect(panel.getByRole('status')).toHaveText('Copied filtered logs')
  const copied = await page.evaluate(() => navigator.clipboard.readText())
  expect(copied).toContain('"category":"EXPORT"')
  expect(copied).toContain('"message":"download record"')
  expect(copied).not.toContain('visible needle record')

  const downloadPromise = page.waitForEvent('download')
  await panel.getByRole('button', { name: 'Download JSONL' }).click()
  const download = await downloadPromise
  const content = await readFile(await download.path(), 'utf8')
  const [header, record] = content.trim().split('\n').map((line) => JSON.parse(line))
  expect(header).toMatchObject({
    type: 'picotracker-log-export', version: 1,
    filter: { minimumSeverity: 'error', category: 'EXPORT', text: 'DOWNLOAD' },
  })
  expect(record).toMatchObject({ severity: 'error', category: 'EXPORT', message: 'download record' })

  await panel.getByLabel('Severity').selectOption('debug')
  await panel.getByLabel('Category').selectOption('')
  await panel.getByLabel('Search').fill('')

  await panel.getByRole('button', { name: 'Pause', exact: true }).click()
  await expect(panel.getByText('paused', { exact: true })).toBeVisible()
  await page.evaluate(() => {
    const severities = ['debug', 'info', 'warn', 'error']
    for (let index = 0; index < 1_000; index += 1) {
      globalThis.__picoTrackerLogsTest.append({
        monotonicUs: 1_000 + index,
        wallTime: 1_700_000_001_000 + index,
        severity: severities[index % severities.length],
        category: index % 2 === 0 ? 'BURST-EVEN' : 'BURST-ODD',
        thread: 'browser',
        message: `high-speed record ${index}`,
      })
    }
    globalThis.__picoTrackerLogsTest.append({
      monotonicUs: 2_001, wallTime: 1_700_000_002_001, severity: 'warn',
      category: 'ACCEPTANCE', thread: 'browser', message: 'arrived while paused',
    })
  })
  const pausedRecord = panel.getByText('arrived while paused', { exact: true })
  await expect(pausedRecord).not.toBeVisible()
  await panel.getByRole('button', { name: 'Resume', exact: true }).click()
  await expect(pausedRecord).toBeVisible()

  // The store accepts the entire burst while presentation is paused, retains
  // a fixed-capacity tail, and reports every overwritten record.
  await expect(panel.getByText(/1000 shown \/ 1000 retained \/ 1000 capacity/)).toBeVisible()
  await expect(panel.getByText(/^[1-9]\d* dropped$/)).toBeVisible()

  await panel.getByRole('button', { name: 'Clear', exact: true }).click()
  await expect(panel.getByText('No log records match the current filter.')).toBeVisible()
  await expect(panel.getByText('0 shown / 0 retained / 1000 capacity')).toBeVisible()
  await expect(panel.getByText('0 dropped')).toBeVisible()
  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
  await expect.poll(() => page.evaluate(() => typeof globalThis.__picoTrackerLogsTest)).toBe('undefined')
})
