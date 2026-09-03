import { expect, test } from '@playwright/test'

const mobileViewports = [
  { name: 'compact portrait', width: 320, height: 480 },
  { name: 'portrait', width: 320, height: 568 },
  { name: 'narrow landscape', width: 480, height: 320 },
  { name: 'landscape', width: 568, height: 320 },
  { name: 'modern phone landscape', width: 844, height: 390 },
  { name: 'large phone landscape', width: 932, height: 430 },
]

const userSections = [
  { menuName: 'Tracker', title: 'Tracker' },
  { menuName: 'Files', title: 'Files' },
  { menuName: 'MIDI', title: 'MIDI' },
  { menuName: 'Settings', title: 'Settings' },
  { menuName: 'About', title: 'About' },
]

const userSectionNames = userSections.map(({ menuName }) => menuName)
const developerSectionNames = ['Logs', 'Trace']

const menuTrigger = (page) => page.getByRole('button', { name: 'Open menu' })
const mobileMenu = (page) => page.getByRole('dialog', { name: 'Menu' })

async function openMenu(page) {
  await menuTrigger(page).click()
  const menu = mobileMenu(page)
  await expect(menu).toBeVisible()
  await expect(menu.getByRole('button', { name: 'Close menu' })).toBeFocused()
  return menu
}

async function sectionNames(navigation) {
  return navigation.getByRole('button').evaluateAll((buttons) =>
    buttons.map((button) => button.getAttribute('aria-label')),
  )
}

async function expectUserSections(menu) {
  const navigation = menu.getByRole('navigation', { name: 'Mobile workspace sections' })
  await expect(navigation.getByRole('button')).toHaveCount(userSectionNames.length)
  expect(await sectionNames(navigation)).toEqual(userSectionNames)
  return navigation
}

async function expectTouchTargets(page, root = page.locator('body')) {
  const tooSmall = await root.locator('button:not(:disabled),a[href],select:not(:disabled),input:not([type="hidden"]):not(.sr-only):not(:disabled)')
    .evaluateAll((elements) => elements.flatMap((element) => {
      const target = ['checkbox', 'radio'].includes(element.getAttribute('type'))
        ? element.closest('label') ?? element
        : element
      const rect = target.getBoundingClientRect()
      const style = getComputedStyle(target)
      if (!rect.width || !rect.height || style.visibility === 'hidden' || style.display === 'none') return []
      return rect.width + 0.01 < 44 || rect.height + 0.01 < 44
        ? [{ tag: target.tagName, label: element.getAttribute('aria-label') || target.textContent?.trim(), width: rect.width, height: rect.height }]
        : []
    }))
  expect(tooSmall).toEqual([])
}

for (const viewport of mobileViewports) {
  test(`tracker stays usable at the ${viewport.name} mobile viewport`, async ({ page }) => {
    await page.setViewportSize(viewport)
    await page.goto('/?audio=disabled')

    const dashboard = page.locator('.dashboard')
    const canvas = page.locator('#picotracker-canvas')
    const controls = page.locator('[data-action]')
    const trigger = menuTrigger(page)

    await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
    await expect(dashboard).toHaveAttribute('data-layout', 'compact')
    await expect(canvas).toHaveAttribute('data-frame-content', 'rendered', { timeout: 20_000 })
    await expect(controls).toHaveCount(8)

    const triggerBox = await trigger.boundingBox()
    expect(triggerBox).not.toBeNull()
    expect(triggerBox.width).toBeGreaterThanOrEqual(44)
    expect(triggerBox.height).toBeGreaterThanOrEqual(44)

    const canvasBox = await canvas.boundingBox()
    expect(canvasBox).not.toBeNull()
    expect(canvasBox.width).toBeGreaterThanOrEqual(240)
    expect(canvasBox.height).toBe(canvasBox.width)
    expect(canvasBox.x).toBeGreaterThanOrEqual(0)
    expect(canvasBox.y).toBeGreaterThanOrEqual(0)
    expect(canvasBox.x + canvasBox.width).toBeLessThanOrEqual(viewport.width)
    expect(canvasBox.y + canvasBox.height).toBeLessThanOrEqual(viewport.height)

    const pageGeometry = await page.evaluate(() => ({
      document: {
        clientWidth: document.documentElement.clientWidth,
        clientHeight: document.documentElement.clientHeight,
        scrollWidth: document.documentElement.scrollWidth,
        scrollHeight: document.documentElement.scrollHeight,
      },
      body: {
        clientWidth: document.body.clientWidth,
        clientHeight: document.body.clientHeight,
        scrollWidth: document.body.scrollWidth,
        scrollHeight: document.body.scrollHeight,
      },
    }))
    expect(pageGeometry).toEqual({
      document: {
        clientWidth: viewport.width,
        clientHeight: viewport.height,
        scrollWidth: viewport.width,
        scrollHeight: viewport.height,
      },
      body: {
        clientWidth: viewport.width,
        clientHeight: viewport.height,
        scrollWidth: viewport.width,
        scrollHeight: viewport.height,
      },
    })

    for (const control of await controls.all()) {
      const box = await control.boundingBox()
      expect(box).not.toBeNull()
      expect(box.width).toBeGreaterThanOrEqual(44)
      expect(box.height).toBeGreaterThanOrEqual(44)
      expect(box.x).toBeGreaterThanOrEqual(0)
      expect(box.y).toBeGreaterThanOrEqual(0)
      expect(box.x + box.width).toBeLessThanOrEqual(viewport.width)
      expect(box.y + box.height).toBeLessThanOrEqual(viewport.height)
    }

    const down = page.getByRole('button', { name: 'Down', exact: true })
    await down.dispatchEvent('pointerdown', { pointerId: 901, pointerType: 'touch' })
    await expect(down).toHaveAttribute('aria-pressed', 'true')
    await page.waitForTimeout(650)
    await expect(down).toHaveAttribute('aria-pressed', 'true')
    await down.dispatchEvent('pointerup', { pointerId: 901, pointerType: 'touch' })
    await expect(down).toHaveAttribute('aria-pressed', 'false')

    const menu = await openMenu(page)
    const workspaceNavigation = await expectUserSections(menu)
    await expect(menu.getByRole('navigation', { name: 'Developer sections' })).toHaveCount(0)
    await expect(menu.getByRole('button', { name: 'Developer tools' })).toHaveAttribute('aria-pressed', 'false')

    for (const button of await workspaceNavigation.getByRole('button').all()) {
      await button.scrollIntoViewIfNeeded()
      const box = await button.boundingBox()
      expect(box).not.toBeNull()
      expect(box.width).toBeGreaterThanOrEqual(44)
      expect(box.height).toBeGreaterThanOrEqual(44)
    }

    await menu.getByRole('button', { name: 'Close menu' }).click()
    await expect(menu).toHaveCount(0)
    await expect(trigger).toBeFocused()
  })
}

test('ordinary mobile users can enter every workspace section without developer tools', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/?audio=disabled')

  const dashboard = page.locator('.dashboard')
  const trigger = menuTrigger(page)
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')

  for (const section of userSections) {
    const menu = await openMenu(page)
    const navigation = await expectUserSections(menu)
    await expect(menu.getByRole('navigation', { name: 'Developer sections' })).toHaveCount(0)
    await navigation.getByRole('button', { name: section.menuName, exact: true }).click()

    await expect(menu).toHaveCount(0)
    await expect(trigger).toBeFocused()
    await expect(page.locator('.current-section')).toHaveText(section.title)
    if (section.menuName === 'Tracker') {
      await expect(page.locator('#picotracker-canvas')).toBeVisible()
    } else {
      await expect(page.getByRole('heading', { name: section.title, exact: true })).toBeVisible()
    }
  }
})

for (const viewport of [{ width: 390, height: 844 }, { width: 844, height: 390 }]) {
  test(`ordinary controls keep 44px hit targets at ${viewport.width}x${viewport.height}`, async ({ page }) => {
    await page.setViewportSize(viewport)
    await page.goto('/?audio=disabled')
    await expect(page.locator('#picotracker-canvas')).toHaveAttribute('data-frame-content', 'rendered', { timeout: 20_000 })

    for (const section of userSections) {
      const menu = await openMenu(page)
      await expectTouchTargets(page, menu)
      await menu.getByRole('navigation', { name: 'Mobile workspace sections' })
        .getByRole('button', { name: section.menuName, exact: true }).click()
      await expectTouchTargets(page)
    }
  })
}

test('developer tools only add Logs and Trace to the mobile menu', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/?audio=disabled')

  const dashboard = page.locator('.dashboard')
  let menu = await openMenu(page)
  const developerToggle = menu.getByRole('button', { name: 'Developer tools' })
  const userNavigation = await expectUserSections(menu)

  await expect(menu.getByRole('navigation', { name: 'Developer sections' })).toHaveCount(0)
  await developerToggle.click()
  await expect(developerToggle).toHaveAttribute('aria-pressed', 'true')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  expect(await sectionNames(userNavigation)).toEqual(userSectionNames)

  const developerNavigation = menu.getByRole('navigation', { name: 'Developer sections' })
  await expect(developerNavigation.getByRole('button')).toHaveCount(developerSectionNames.length)
  expect(await sectionNames(developerNavigation)).toEqual(developerSectionNames)

  await developerNavigation.getByRole('button', { name: 'Logs', exact: true }).click()
  await expect(page.getByRole('heading', { name: 'Logs', exact: true })).toBeVisible()
  await expectTouchTargets(page)

  menu = await openMenu(page)
  await menu.getByRole('navigation', { name: 'Developer sections' })
    .getByRole('button', { name: 'Trace', exact: true }).click()
  await expect(page.getByRole('heading', { name: 'Performance Trace', exact: true })).toBeVisible()
  await expectTouchTargets(page)

  menu = await openMenu(page)
  await menu.getByRole('button', { name: 'Developer tools' }).click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  await expect(menu.getByRole('navigation', { name: 'Developer sections' })).toHaveCount(0)
  await menu.getByRole('button', { name: 'Close menu' }).click()
  await expect(page.locator('.current-section')).toHaveText('Tracker')
  await expect(page.locator('#picotracker-canvas')).toBeVisible()
})

test('mobile menu traps focus and restores the trigger after every close path', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/?audio=disabled')

  const trigger = menuTrigger(page)
  const canvas = page.locator('#picotracker-canvas')
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered', { timeout: 20_000 })
  let menu = await openMenu(page)
  const close = menu.getByRole('button', { name: 'Close menu' })
  const developerToggle = menu.getByRole('button', { name: 'Developer tools' })

  const generationBeforeMenuKey = await canvas.getAttribute('data-action-generation')
  await page.keyboard.press('KeyW')
  await expect(canvas).toHaveAttribute('data-action-generation', generationBeforeMenuKey)

  // A modal menu keeps background controls out of the focus order.
  await trigger.evaluate((element) => element.focus())
  await expect(close).toBeFocused()

  await page.keyboard.press('Shift+Tab')
  await expect(developerToggle).toBeFocused()
  await page.keyboard.press('Tab')
  await expect(close).toBeFocused()

  await page.mouse.click(2, 2)
  await expect(menu).toHaveCount(0)
  await expect(trigger).toBeFocused()

  menu = await openMenu(page)
  await page.keyboard.press('Escape')
  await expect(menu).toHaveCount(0)
  await expect(trigger).toBeFocused()

  menu = await openMenu(page)
  await menu.getByRole('button', { name: 'Close menu' }).click()
  await expect(menu).toHaveCount(0)
  await expect(trigger).toBeFocused()
})

test('resizing and rotating only change layout, never the developer preference', async ({ page }) => {
  const dashboard = page.locator('.dashboard')

  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/?audio=disabled')
  await expect(dashboard).toHaveAttribute('data-layout', 'compact')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')

  // Rotating between two compact layouts keeps an open modal menu coherent:
  // it remains visible, focused, and the workspace remains inert.
  let menu = await openMenu(page)
  await page.setViewportSize({ width: 844, height: 390 })
  await expect(menu).toBeVisible()
  await expect(page.locator('.workspace')).toHaveAttribute('inert', '')
  await expect(menu.getByRole('button', { name: 'Close menu' })).toBeFocused()
  await menu.getByRole('button', { name: 'Close menu' }).click()

  await page.setViewportSize({ width: 1024, height: 768 })
  await expect(dashboard).toHaveAttribute('data-layout', 'desktop')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
  const desktopNavigation = page.getByRole('navigation', { name: 'Main navigation' })
  expect(await sectionNames(desktopNavigation)).toEqual(userSectionNames)

  await page.setViewportSize({ width: 844, height: 390 })
  await expect(dashboard).toHaveAttribute('data-layout', 'compact')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')

  menu = await openMenu(page)
  await menu.getByRole('button', { name: 'Developer tools' }).click()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await menu.getByRole('button', { name: 'Close menu' }).click()

  await page.setViewportSize({ width: 390, height: 844 })
  await expect(dashboard).toHaveAttribute('data-layout', 'compact')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  menu = await openMenu(page)
  expect(await sectionNames(menu.getByRole('navigation', { name: 'Mobile workspace sections' })))
    .toEqual(userSectionNames)
  expect(await sectionNames(menu.getByRole('navigation', { name: 'Developer sections' })))
    .toEqual(developerSectionNames)
  await menu.getByRole('button', { name: 'Close menu' }).click()

  await page.setViewportSize({ width: 1024, height: 768 })
  await expect(dashboard).toHaveAttribute('data-layout', 'desktop')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  expect(await sectionNames(page.getByRole('navigation', { name: 'Main navigation' })))
    .toEqual(['Tracker', 'Files', 'MIDI', 'Logs', 'Trace', 'Settings', 'About'])

  await page.reload()
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
})

test('forced developer mode preserves an explicit disabled preference', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await page.goto('/?audio=disabled')
  await page.evaluate(() => localStorage.setItem(
    'picotracker.wasm.settings.v4',
    JSON.stringify({ developerMode: false }),
  ))
  await page.goto('/?audio=disabled&dev=1')

  const dashboard = page.locator('.dashboard')
  const menu = await openMenu(page)
  const toggle = menu.getByRole('button', { name: 'Developer tools' })
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'true')
  await expect(toggle).toHaveAttribute('aria-pressed', 'true')
  await expect(toggle).toBeDisabled()
  await expect.poll(() => page.evaluate(() => JSON.parse(
    localStorage.getItem('picotracker.wasm.settings.v4'),
  ).developerMode)).toBe(false)

  await page.goto('/?audio=disabled')
  await expect(dashboard).toHaveAttribute('data-developer-mode', 'false')
})

test('short-screen runtime recovery remains fully visible and restarts in place', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 480 })
  await page.goto('/?audio=disabled&runtime-fail-test=1')

  const workspace = page.locator('.workspace')
  const recovery = page.locator('[data-recovery-kind="runtime"]')
  const retry = page.getByRole('button', { name: 'Retry runtime' })
  const simulator = page.locator('.device-stage')
  const canvas = page.locator('#picotracker-canvas')
  await expect(recovery).toBeVisible({ timeout: 20_000 })
  await expect(retry).toBeFocused()
  await expect(simulator).toHaveAttribute('inert', '')
  await expect(page.getByRole('region', { name: 'NullPerator player' })).toHaveCount(0)
  await canvas.evaluate((element) => element.focus())
  await expect(retry).toBeFocused()
  await expect.poll(() => workspace.evaluate((element) => element.scrollTop)).toBe(0)
  const workspaceBox = await workspace.boundingBox()
  const recoveryBox = await recovery.boundingBox()
  expect(recoveryBox.y).toBeGreaterThanOrEqual(workspaceBox.y)
  await expect(retry).toBeInViewport()

  const down = page.locator('[data-action="down"]')
  await page.keyboard.down('s')
  await expect(down).toHaveAttribute('aria-pressed', 'false')
  await page.keyboard.up('s')

  await retry.click()
  await expect(canvas).toHaveAttribute('data-frame-content', 'rendered', { timeout: 20_000 })
  await expect(simulator).not.toHaveAttribute('inert', '')
  await expect(canvas).toBeFocused()
  await expect.poll(() => workspace.evaluate((element) => element.scrollTop)).toBe(0)
})
