import type { QueryDef } from '../api'
import { categoryLink, clientLink, transactionsLink } from '../drill'
import { shortCategory } from '../format'
import type { DateRange } from '../periods'
import { linkHandler } from '../router'

// How names link, everywhere: a client or a category opens its own page; an account, which has no
// page, opens the transactions being looked at, narrowed to that account.

// The period a query covers, for a category page opened from it.
export function periodOf(def: QueryDef): string | DateRange {
  if (def.relative_period) return def.relative_period
  if (def.date_from && def.date_to) return { from: def.date_from, to: def.date_to }
  return 'all'
}

// `account` is the full "Bank::Name" (a transaction's account_full); the short name shows.
export function AccountLink({ def, account, href = transactionsLink({ def: { ...def, accounts: [account] } }) }: { def?: QueryDef; account: string; href?: string }) {
  return (
    <a className="quiet-link" href={href} onClick={linkHandler(href)} title={`Only ${account}`}>
      {account.slice(account.lastIndexOf('::') + 2)}
    </a>
  )
}

export function CategoryTag({ id, name, period }: { id: number; name: string; period?: string | DateRange }) {
  const href = categoryLink(id, period)
  return (
    <a className="tag" href={href} onClick={linkHandler(href)} title={name}>
      {shortCategory(name)}
    </a>
  )
}

export function ClientName({ id, name, className = 'strong clip' }: { id: number; name: string; className?: string }) {
  if (!name) return <div className="strong">—</div>
  const href = clientLink(id)
  return (
    <a className={className} href={href} onClick={linkHandler(href)}>
      {name}
    </a>
  )
}
