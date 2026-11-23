// Stroke icons (24px grid), coloured by currentColor.
const paths = {
  logo: 'M3 10l9-6 9 6M5 10v8M9.5 10v8M14.5 10v8M19 10v8M3 20h18',
  overview: 'M3 12l9-8 9 8M5 10v10h14V10',
  explore: 'M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14M21 21l-5-5',
  transactions: 'M4 6h16M4 12h16M4 18h10',
  reports: 'M5 20V10M12 20V4M19 20v-7',
  chevron: 'M9 6l6 6-6 6',
  download: 'M12 4v11M7 10l5 5 5-5M5 20h14',
  close: 'M6 6l12 12M18 6L6 18',
  logout: 'M15 4h4v16h-4M10 8l-4 4 4 4M6 12h10',
  settings: 'M4 6h9M17 6h3M15 4v4M4 12h3M11 12h9M9 10v4M4 18h11M19 18h1M17 16v4',
  trash: 'M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3',
  imports: 'M12 16V4M7 9l5-5 5 5M5 20h14',
  edit: 'M4 20h4L19 9l-4-4L4 16v4M14 6l4 4',
} as const

export type IconName = keyof typeof paths

export function Icon({ name, size = 18 }: { name: IconName; size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d={paths[name]} />
    </svg>
  )
}
