import { useId, useState } from 'react'
import { Icon } from './Icons'

// Chips for a list of names (categories, clients, ...), typed with autocomplete from `options`.
// Free text is allowed too: the daemon matches names fuzzily.
// `display` names a value the chip can't show as-is (an exact "#12" filter from a drill-down).
export function NamePicker({
  label,
  values,
  options,
  onChange,
  placeholder,
  display = (v) => v,
}: {
  label: string
  values: string[]
  options: string[]
  onChange: (v: string[]) => void
  placeholder?: string
  display?: (value: string) => string
}) {
  const id = useId()
  const [text, setText] = useState('')
  const add = (name: string) => {
    const v = name.trim()
    if (v && !values.includes(v)) onChange([...values, v])
    setText('')
  }
  return (
    <div className="field">
      <label htmlFor={id}>{label}</label>
      <div className="chip-input">
        {values.map((v) => (
          <span key={v} className="chip">
            {display(v)}
            <button type="button" aria-label={`Remove ${display(v)}`} onClick={() => onChange(values.filter((x) => x !== v))}>
              <Icon name="close" size={12} />
            </button>
          </span>
        ))}
        <input
          id={id}
          list={`${id}-options`}
          value={text}
          placeholder={values.length ? '' : placeholder}
          onChange={(e) => {
            // Picking a datalist suggestion fires a change with the full option text.
            if (options.includes(e.target.value)) add(e.target.value)
            else setText(e.target.value)
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ',') {
              e.preventDefault()
              add(text)
            } else if (e.key === 'Backspace' && !text && values.length) {
              onChange(values.slice(0, -1))
            }
          }}
          onBlur={() => text && add(text)}
        />
        <datalist id={`${id}-options`}>
          {/* Two clients can share a name: one suggestion is enough. */}
          {[...new Set(options)].map((o) => (
            <option key={o} value={o} />
          ))}
        </datalist>
      </div>
    </div>
  )
}
