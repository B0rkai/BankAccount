import { useEffect, useState } from 'react'
import { getDataVersion, onDataChanged } from './api'

export interface Async<T> {
  data: T | undefined
  error: string | undefined
  loading: boolean
}

// The data version (api.ts): changes after every edit made here.
export function useDataVersion(): number {
  const [version, setVersion] = useState(getDataVersion)
  useEffect(() => onDataChanged(() => setVersion(getDataVersion())), [])
  return version
}

// Runs `load` whenever `key` changes - or the data does, after an edit - dropping results of a
// superseded run.
export function useAsync<T>(load: () => Promise<T>, key: string): Async<T> {
  const [state, setState] = useState<Async<T>>({ data: undefined, error: undefined, loading: true })
  const version = useDataVersion()
  useEffect(() => {
    let live = true
    setState((s) => ({ ...s, loading: true, error: undefined }))
    load().then(
      (data) => live && setState({ data, error: undefined, loading: false }),
      (e: Error) => live && setState({ data: undefined, error: e.message, loading: false }),
    )
    return () => {
      live = false
    }
  }, [key, version])
  return state
}

// A per-browser preference - storage may be unavailable (private mode), so this degrades to
// in-memory state.
export function useStoredState<T>(name: string, initial: T): [T, (v: T) => void] {
  const [value, setValue] = useState<T>(() => {
    try {
      const raw = localStorage.getItem(`bankaccount.${name}`)
      return raw ? (JSON.parse(raw) as T) : initial
    } catch {
      return initial
    }
  })
  const set = (v: T) => {
    setValue(v)
    try {
      localStorage.setItem(`bankaccount.${name}`, JSON.stringify(v))
    } catch {
      // ignore
    }
  }
  return [value, set]
}

// Chart.js draws on a canvas, so it needs real colours rather than CSS variables.
export function useThemeColours() {
  const read = () => {
    const s = getComputedStyle(document.documentElement)
    return {
      text: s.getPropertyValue('--text').trim() || '#15181d',
      muted: s.getPropertyValue('--muted').trim() || '#5b6370',
      grid: s.getPropertyValue('--grid').trim() || '#eef0f3',
    }
  }
  const [colours, setColours] = useState(read)
  useEffect(() => {
    const mq = window.matchMedia('(prefers-color-scheme: dark)')
    const onChange = () => setColours(read())
    mq.addEventListener('change', onChange)
    return () => mq.removeEventListener('change', onChange)
  }, [])
  return colours
}

// Phone width (the CSS's max-width: 760px breakpoint), following resizes and rotation.
export function useNarrow(): boolean {
  const query = '(max-width: 760px)'
  const [narrow, setNarrow] = useState(() => window.matchMedia(query).matches)
  useEffect(() => {
    const mq = window.matchMedia(query)
    const onChange = () => setNarrow(mq.matches)
    mq.addEventListener('change', onChange)
    return () => mq.removeEventListener('change', onChange)
  }, [])
  return narrow
}
