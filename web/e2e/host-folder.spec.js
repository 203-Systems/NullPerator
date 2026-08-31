import { expect, test } from '@playwright/test'
import { restartWorkbench, stopWorkbench } from './helpers/runtime.js'

const text = new TextEncoder()

async function resetHostMirror(page) {
  await page.goto('/oracle.html')
  await page.evaluate(async () => {
    const root = await navigator.storage.getDirectory()
    for await (const [name] of root.entries()) await root.removeEntry(name, { recursive: true })
    for (const name of ['/data', 'picotracker-host-folder-v1']) {
      await new Promise((resolve, reject) => {
        const request = indexedDB.deleteDatabase(name)
        request.onsuccess = () => resolve()
        request.onerror = () => reject(request.error)
        request.onblocked = () => reject(new Error(`IndexedDB cleanup blocked: ${name}`))
      })
    }
  })
}

async function writeHostFile(page, path, bytes) {
  await page.evaluate(async ({ path, bytes }) => {
    const root = await navigator.storage.getDirectory()
    const handle = await root.getFileHandle(path, { create: true })
    const writable = await handle.createWritable()
    await writable.write(new Uint8Array(bytes))
    await writable.close()
  }, { path, bytes: Array.from(bytes) })
}

async function readHostFile(page, path) {
  return page.evaluate(async (path) => {
    const root = await navigator.storage.getDirectory()
    const handle = await root.getFileHandle(path)
    const file = await handle.getFile()
    return Array.from(new Uint8Array(await file.arrayBuffer()))
  }, path)
}

async function removeHostFile(page, path) {
  await page.evaluate(async (path) => {
    const root = await navigator.storage.getDirectory()
    await root.removeEntry(path)
  }, path)
}

async function hostFileExists(page, path) {
  return page.evaluate(async (path) => {
    try {
      const root = await navigator.storage.getDirectory()
      await root.getFileHandle(path)
      return true
    } catch (error) {
      if (error?.name === 'NotFoundError') return false
      throw error
    }
  }, path)
}

async function runSuccessfulHostAction(page, name) {
  const region = page.getByRole('region', { name: 'Host folder mirror' })
  const last = region.getByText(/Last successful sync:/)
  const previous = await last.count() ? await last.textContent() : ''
  await page.getByRole('button', { name, exact: true }).click()
  await expect.poll(async () => {
    const current = await last.textContent().catch(() => '')
    return current && current !== previous ? current : null
  }, { timeout: 20_000 }).not.toBeNull()
}

async function resolveSuccessfulConflict(page, name, doubleActivate = false) {
  const region = page.getByRole('region', { name: 'Host folder mirror' })
  const last = region.getByText(/Last successful sync:/)
  const previous = await last.count() ? await last.textContent() : ''
  const action = page.getByRole('button', { name, exact: true })
  if (doubleActivate) await action.dblclick()
  else await action.click()
  await expect.poll(async () => {
    const current = await last.textContent().catch(() => '')
    return current && current !== previous ? current : null
  }, { timeout: 20_000 }).not.toBeNull()
}

test('host-folder mirror mounts an OPFS test handle, synchronizes, and resolves a visible conflict', async ({ page }) => {
  test.setTimeout(110_000)
  await page.addInitScript(() => {
    localStorage.setItem('picotracker-host-permission', localStorage.getItem('picotracker-host-permission') ?? 'granted')
    if (globalThis.FileSystemDirectoryHandle) {
      Object.defineProperty(FileSystemDirectoryHandle.prototype, 'queryPermission', {
        configurable: true,
        value: async () => localStorage.getItem('picotracker-host-permission') ?? 'prompt',
      })
      Object.defineProperty(FileSystemDirectoryHandle.prototype, 'requestPermission', {
        configurable: true,
        value: async () => {
          localStorage.setItem('picotracker-host-permission', 'granted')
          return 'granted'
        },
      })
    }
    globalThis.showDirectoryPicker = async () => navigator.storage.getDirectory()
  })
  await resetHostMirror(page)
  await writeHostFile(page, 'host-note.dat', text.encode('host-v1'))
  await writeHostFile(page, 'host-second.dat', text.encode('host-second-v1'))

  await page.goto('/?storage-test=1')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await page.getByRole('button', { name: 'Mount folder' }).click()
  await expect(page.locator('[data-host-folder-state="mounted"]')).toBeVisible()
  await runSuccessfulHostAction(page, 'Sync')
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.read('/data/host-note.dat'))).toEqual(Array.from(text.encode('host-v1')))

  await writeHostFile(page, 'host-pull.dat', text.encode('pulled'))
  await runSuccessfulHostAction(page, 'Pull')
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.read('/data/host-pull.dat'))).toEqual(Array.from(text.encode('pulled')))

  await removeHostFile(page, 'host-pull.dat')
  await runSuccessfulHostAction(page, 'Pull')
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.exists('/data/host-pull.dat'))).toBe(false)

  await page.evaluate(async (bytes) => {
    globalThis.__picoTrackerStorageTest.write('/data/browser-push.dat', bytes)
    await globalThis.__picoTrackerStorageTest.flush()
  }, Array.from(text.encode('pushed')))
  await runSuccessfulHostAction(page, 'Push')
  await expect.poll(() => readHostFile(page, 'browser-push.dat')).toEqual(Array.from(text.encode('pushed')))

  const ready = page.locator('[data-runtime-state="ready"]')
  await restartWorkbench(page)
  await ready.waitFor({ state: 'hidden', timeout: 10_000 })
  await expect(ready).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await expect(page.locator('[data-host-folder-state="mounted"]')).toBeVisible({ timeout: 10_000 })

  page.once('dialog', (dialog) => dialog.accept())
  await page.getByText('browser-push.dat', { exact: true }).locator('..').getByRole('button', { name: 'Delete' }).click()
  await expect(page.getByText('browser-push.dat', { exact: true })).not.toBeVisible()
  await expect(page.locator('.file-sync')).toHaveAttribute('data-storage-dirty', 'false')
  await runSuccessfulHostAction(page, 'Push')
  await expect.poll(() => hostFileExists(page, 'browser-push.dat')).toBe(false)

  await page.evaluate(async (bytes) => {
    globalThis.__picoTrackerStorageTest.write('/data/host-note.dat', bytes)
    await globalThis.__picoTrackerStorageTest.flush()
  }, Array.from(text.encode('browser-v2')))
  await page.evaluate(async (bytes) => {
    globalThis.__picoTrackerStorageTest.write('/data/host-second.dat', bytes)
    await globalThis.__picoTrackerStorageTest.flush()
  }, Array.from(text.encode('browser-second-v2')))
  await writeHostFile(page, 'host-note.dat', text.encode('host-v2'))
  await writeHostFile(page, 'host-second.dat', text.encode('host-second-v2'))
  await page.getByRole('button', { name: 'Sync', exact: true }).click()
  const conflictDialog = page.getByRole('dialog', { name: 'Host-folder conflict' })
  await expect(conflictDialog).toBeVisible({ timeout: 20_000 })
  await expect.poll(() => conflictDialog.evaluate((element) => element.matches(':modal'))).toBe(true)
  await expect(page.getByRole('button', { name: 'Keep browser version' })).toBeFocused()

  // A parent view can be removed for reasons outside the modal itself (for
  // example, responsive mode changes). Removing an open native dialog must
  // release the top layer and allow the replacement surface to take focus.
  await page.getByRole('button', { name: 'Device', exact: true }).evaluate((element) => element.click())
  await expect(conflictDialog).toHaveCount(0)
  await expect(page.locator('dialog:modal')).toHaveCount(0)
  const filesToggle = page.getByRole('button', { name: 'Toggle Files tool' })
  await filesToggle.click()
  await expect(conflictDialog).toBeVisible()
  await expect(page.getByRole('button', { name: 'Keep browser version' })).toBeFocused()
  await page.getByRole('button', { name: 'Close Files tool' }).evaluate((element) => element.click())
  await expect(conflictDialog).toHaveCount(0)
  await expect(page.locator('dialog:modal')).toHaveCount(0)
  await expect(filesToggle).toBeFocused()
  await filesToggle.click()
  await expect(conflictDialog).toBeVisible()
  await expect(page.getByRole('button', { name: 'Keep browser version' })).toBeFocused()

  const firstConflictPath = await conflictDialog.locator('code').textContent()
  await page.getByRole('button', { name: 'Keep browser version' }).click()
  await expect.poll(async () => {
    const path = await conflictDialog.locator('code').textContent().catch(() => null)
    return path && path !== firstConflictPath ? path : null
  }).not.toBeNull()
  await expect(page.getByRole('button', { name: 'Keep browser version' })).toBeFocused()
  await resolveSuccessfulConflict(page, 'Keep browser version', true)
  await expect(page.locator('[data-host-folder-state="mounted"]')).toBeVisible({ timeout: 20_000 })
  await expect(filesToggle).toBeFocused()
  await expect(page.getByText('Unknown host-folder conflict')).toHaveCount(0)
  await expect.poll(() => readHostFile(page, 'host-note.dat')).toEqual(Array.from(text.encode('browser-v2')))
  await expect.poll(() => readHostFile(page, 'host-second.dat')).toEqual(Array.from(text.encode('browser-second-v2')))

  await page.evaluate(async (bytes) => {
    globalThis.__picoTrackerStorageTest.write('/data/host-note.dat', bytes)
    await globalThis.__picoTrackerStorageTest.flush()
  }, Array.from(text.encode('browser-v3')))
  await writeHostFile(page, 'host-note.dat', text.encode('host-v3'))
  await page.getByRole('button', { name: 'Sync', exact: true }).click()
  await expect(page.getByRole('dialog', { name: 'Host-folder conflict' })).toBeVisible({ timeout: 20_000 })
  await expect(page.getByRole('button', { name: 'Keep browser version' })).toBeFocused()
  await resolveSuccessfulConflict(page, 'Keep host version')
  await expect.poll(() => page.evaluate(() => globalThis.__picoTrackerStorageTest.read('/data/host-note.dat'))).toEqual(Array.from(text.encode('host-v3')))

  await page.evaluate(async (bytes) => {
    globalThis.__picoTrackerStorageTest.write('/data/host-note.dat', bytes)
    await globalThis.__picoTrackerStorageTest.flush()
  }, Array.from(text.encode('browser-v4')))
  await writeHostFile(page, 'host-note.dat', text.encode('host-v4'))
  await page.getByRole('button', { name: 'Sync', exact: true }).click()
  await expect(page.getByRole('dialog', { name: 'Host-folder conflict' })).toBeVisible({ timeout: 20_000 })
  await expect(page.getByRole('button', { name: 'Keep browser version' })).toBeFocused()
  await resolveSuccessfulConflict(page, 'Keep both')
  await expect.poll(() => readHostFile(page, 'host-note (browser).dat')).toEqual(Array.from(text.encode('browser-v4')))
  await expect.poll(() => readHostFile(page, 'host-note (host).dat')).toEqual(Array.from(text.encode('host-v4')))

  await page.evaluate(() => localStorage.setItem('picotracker-host-permission', 'denied'))
  await page.reload()
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: 'Files', exact: true }).click()
  await expect(page.locator('[data-host-folder-state="denied"]')).toBeVisible({ timeout: 10_000 })
  await page.getByRole('button', { name: 'Reconnect folder' }).click()
  await expect(page.locator('[data-host-folder-state="mounted"]')).toBeVisible({ timeout: 10_000 })

  await page.getByRole('button', { name: 'Unmount folder' }).click()
  await expect(page.locator('[data-host-folder-state="unmounted"]')).toBeVisible()
  await stopWorkbench(page)
  await expect(page.locator('[data-runtime-state="idle"]')).toBeVisible({ timeout: 10_000 })
})
