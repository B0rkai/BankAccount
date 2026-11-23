import { useEffect, useState } from 'react'
import { api, cachedNameList, type QueryDef, type WebSettings } from './api'

// The settings page's choices, shared by every page: loaded once, saved to the daemon (so the phone
// and the LAN browser agree), and pushed to every page using them as soon as they change. They hold
// category ids, never names, so a renamed category keeps its place in them.

let current: WebSettings | undefined
let loading: Promise<WebSettings> | undefined
const listeners = new Set<(s: WebSettings) => void>()

// Before the settings were kept on the server, the overview's left-out categories were names in
// this browser's storage. The first load turns them into ids and saves them on the server.
async function fromBrowser(): Promise<number[] | undefined> {
  let names: unknown
  try {
    const raw = window.localStorage.getItem('overview.excluded')
    names = raw ? JSON.parse(raw) : undefined
  } catch {
    return undefined
  }
  if (!Array.isArray(names) || !names.length) return undefined
  const list = await cachedNameList('categories')
  return names.flatMap((n) => {
    const i = list.names.indexOf(n as string)
    return i >= 0 && list.ids?.[i] !== undefined ? [list.ids[i]] : []
  })
}

async function load(): Promise<WebSettings> {
  const settings: WebSettings = { ...(await api.settings()) }
  if (!Array.isArray(settings.excluded_categories)) {
    const moved = await fromBrowser().catch(() => undefined)
    if (moved) {
      settings.excluded_categories = moved
      await api.saveSettings(settings).catch(() => {})
    }
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
    loadSettings().then(setValue, () => setValue({}))
    return () => {
      listeners.delete(setValue)
    }
  }, [])
  return value
}

export const asFilter = (ids: number[]) => ids.map((id) => `#${id}`)

// A query leaving out the categories chosen in settings.
export function withoutExcluded(def: QueryDef, excluded: number[]): QueryDef {
  return excluded.length ? { ...def, categories: asFilter(excluded), exclude_categories: true } : def
}

// Money in under the categories chosen as "not income" in settings is shown apart. `real` and
// `other` narrow a query (already leaving out the excluded categories) to each part; undefined
// when nothing was chosen, as all of it is income then.
export interface IncomeSplit {
  real: (def: QueryDef) => QueryDef
  other: (def: QueryDef) => QueryDef
}

export function incomeSplit(settings: WebSettings | undefined): IncomeSplit | undefined {
  const excluded = settings?.excluded_categories ?? []
  const notIncome = (settings?.other_income_categories ?? []).filter((id) => !excluded.includes(id))
  if (!notIncome.length) return undefined
  return {
    real: (def) => ({ ...def, categories: asFilter([...excluded, ...notIncome]), exclude_categories: true }),
    other: (def) => ({ ...def, categories: asFilter(notIncome), exclude_categories: undefined }),
  }
}
