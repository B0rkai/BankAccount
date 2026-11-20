import { useEffect, useState } from 'react'

export interface Async<T> {
  data: T | undefined
  error: string | undefined
  loading: boolean
}

// Runs `load` whenever `key` changes, dropping results of a superseded run.
export function useAsync<T>(load: () => Promise<T>, key: string): Async<T> {
  const [state, setState] = useState<Async<T>>({ data: undefined, error: undefined, loading: true })
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
  }, [key])
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
