import { useEffect, useState } from 'react'
import { api, cachedNames, type QueryDef, type WebSettings } from './api'
import type { NameIndex } from './drill'

// The settings page's choices, shared by every page: loaded once, saved to the daemon (so the phone
// and the LAN browser agree), and pushed to every page using them as soon as they change.

let current: WebSettings | undefined
let loading: Promise<WebSettings> | undefined
const listeners = new Set<(s: WebSettings) => void>()

// Before the settings were kept on the server, the overview's left-out categories lived in this
// browser's storage; the first load picks them up from there.
function fromBrowser(): string[] | undefined {
  try {
    const raw = window.localStorage.getItem('overview.excluded')
    const value = raw ? (JSON.parse(raw) as unknown) : undefined
    return Array.isArray(value) ? (value as string[]) : undefined
  } catch {
    return undefined
  }
}

const DEFAULT_EXCLUDED = /internal transfer|belső átvezetés/i

async function load(): Promise<WebSettings> {
  const [saved, categories] = await Promise.all([api.settings(), cachedNames('categories').catch(() => [] as string[])])
  const settings: WebSettings = { ...saved }
  if (!Array.isArray(settings.excluded_categories)) {
    settings.excluded_categories = fromBrowser() ?? categories.filter((c) => DEFAULT_EXCLUDED.test(c))
  }
  return settings
}

function publish(s: WebSettings) {
  current = s
  listeners.forEach((l) => l(s))
}

export function loadSettings(): Promise<WebSettings> {
  if (current) return Promise.resolve(current)
  loading ??= load().then(
    (s) => {
      publish(s)
      return s
    },
    (e) => {
      loading = undefined
      throw e
    },
  )
  return loading
}

export async function saveSettings(change: Partial<WebSettings>): Promise<void> {
  const next = { ...(current ?? (await loadSettings())), ...change }
  await api.saveSettings(next)
  publish(next)
}

// undefined until loaded; a load error leaves the defaults (nothing left out, all income counts).
export function useSettings(): WebSettings | undefined {
  const [value, setValue] = useState<WebSettings | undefined>(current)
  useEffect(() => {
    listeners.add(setValue)
    loadSettings().then(setValue, () => setValue({ excluded_categories: [] }))
    return () => {
      listeners.delete(setValue)
    }
  }, [])
  return value
}

// Category names as exact "#id" filters (a name the db no longer has is kept as typed).
function exact(names: string[], index: NameIndex | undefined): string[] {
  return names.flatMap((n) => index?.categoryIds.get(n)?.map((id) => `#${id}`) ?? [n])
}

// What counts as income: the categories chosen in settings, or everything when none are chosen.
// `real` and `other` narrow a query (already leaving out the excluded categories) to each part.
export interface IncomeSplit {
  real: (def: QueryDef) => QueryDef | undefined // undefined: no income category chosen, so none
  other: (def: QueryDef) => QueryDef
}

export function incomeSplit(settings: WebSettings | undefined, index: NameIndex | undefined): IncomeSplit | undefined {
  const chosen = settings?.income_categories
  if (!chosen || !index) return undefined
  const excluded = new Set(settings?.excluded_categories ?? [])
  const income = exact(
    chosen.filter((c) => !excluded.has(c)),
    index,
  )
  const left = exact([...excluded], index)
  return {
    real: (def) => (income.length ? { ...def, categories: income, exclude_categories: undefined } : undefined),
    other: (def) => ({ ...def, categories: [...left, ...income], exclude_categories: true }),
  }
}
