const LOCALE = 'hu-HU'

const moneyFormats = new Map<string, Intl.NumberFormat>()

export function formatMoney(value: number, currency: string, opts: { sign?: boolean } = {}): string {
  const key = `${currency}|${opts.sign ? 1 : 0}`
  let fmt = moneyFormats.get(key)
  if (!fmt) {
    const digits = currency === 'HUF' ? 0 : 2
    try {
      fmt = new Intl.NumberFormat(LOCALE, {
        style: 'currency',
        currency,
        minimumFractionDigits: digits,
        maximumFractionDigits: digits,
        signDisplay: opts.sign ? 'exceptZero' : 'auto',
      })
    } catch {
      fmt = new Intl.NumberFormat(LOCALE, { maximumFractionDigits: digits, signDisplay: opts.sign ? 'exceptZero' : 'auto' })
    }
    moneyFormats.set(key, fmt)
  }
  return fmt.format(value)
}

// "4,6 M Ft" - for chart axes and tight spots.
export function formatMoneyCompact(value: number, currency: string): string {
  const abs = Math.abs(value)
  const sign = value < 0 ? '−' : ''
  const unit = currency === 'HUF' ? ' Ft' : ` ${currency}`
  if (abs >= 1e6) return `${sign}${(abs / 1e6).toLocaleString(LOCALE, { maximumFractionDigits: 1 })} M${unit}`
  if (abs >= 1e4) return `${sign}${(abs / 1e3).toLocaleString(LOCALE, { maximumFractionDigits: 0 })} e${unit}`
  return `${sign}${abs.toLocaleString(LOCALE, { maximumFractionDigits: currency === 'HUF' ? 0 : 2 })}${unit}`
}

export function formatCount(n: number): string {
  return n.toLocaleString(LOCALE)
}

export function formatDate(iso: string, opts: { year?: boolean } = {}): string {
  const d = new Date(`${iso}T00:00:00`)
  if (Number.isNaN(d.getTime())) return iso
  return d.toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: opts.year ? 'numeric' : undefined })
}

export function formatDateTime(iso: string): string {
  const d = new Date(iso)
  if (Number.isNaN(d.getTime())) return iso
  return d.toLocaleString('en-GB', { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' })
}

// Relative period keywords the daemon resolves (RelativePeriod.h).
export const RELATIVE_PERIODS: { key: string; label: string }[] = [
  { key: 'this_month', label: 'This month' },
  { key: 'last_month', label: 'Last month' },
  { key: 'last_30_days', label: 'Last 30 days' },
  { key: 'this_quarter', label: 'This quarter' },
  { key: 'last_quarter', label: 'Last quarter' },
  { key: 'this_half', label: 'This half-year' },
  { key: 'last_half', label: 'Last half-year' },
  { key: 'this_year', label: 'This year' },
  { key: 'last_year', label: 'Last year' },
  { key: 'last_12_months', label: 'Last 12 months' },
  { key: 'last_3_whole_years', label: 'Last 3 whole years' },
  { key: 'last_5_whole_years', label: 'Last 5 whole years' },
]

export function periodLabel(key: string): string {
  return RELATIVE_PERIODS.find((p) => p.key === key)?.label ?? key.replace(/_/g, ' ')
}

// The heading the overview shows for a period, e.g. "November 2026" for this_month.
export function periodTitle(key: string, today = new Date()): string {
  const month = (offset: number) => {
    const d = new Date(today.getFullYear(), today.getMonth() + offset, 1)
    return d.toLocaleDateString('en-GB', { month: 'long', year: 'numeric' })
  }
  switch (key) {
    case 'this_month':
      return month(0)
    case 'last_month':
      return month(-1)
    case 'this_year':
      return String(today.getFullYear())
    case 'last_year':
      return String(today.getFullYear() - 1)
    default:
      return periodLabel(key)
  }
}

// "Living expenses::Groceries" -> "Groceries"
export function shortCategory(name: string): string {
  const i = name.lastIndexOf('::')
  return i >= 0 ? name.slice(i + 2) : name
}
