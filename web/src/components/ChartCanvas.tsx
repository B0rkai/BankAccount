import Chart from 'chart.js/auto'
import type { ChartConfiguration } from 'chart.js'
import { useEffect, useRef } from 'react'

// `onPick(index, datasetIndex)` makes the chart clickable: the bar/slice/point under the pointer.
export function ChartCanvas({
  config,
  height = 300,
  label,
  onPick,
}: {
  config: ChartConfiguration
  height?: number
  label: string
  onPick?: (index: number, datasetIndex: number) => void
}) {
  const canvas = useRef<HTMLCanvasElement>(null)
  const pick = useRef(onPick)
  pick.current = onPick
  useEffect(() => {
    if (!canvas.current) return
    const chart = new Chart(canvas.current, {
      ...config,
      options: {
        ...config.options,
        onClick: (_event, elements) => {
          const hit = elements[0]
          if (hit && pick.current) pick.current(hit.index, hit.datasetIndex)
        },
        onHover: (event, elements) => {
          const target = event.native?.target as HTMLElement | undefined
          if (target) target.style.cursor = elements.length && pick.current ? 'pointer' : 'default'
        },
      },
    } as ChartConfiguration)
    return () => chart.destroy()
  }, [config])
  return (
    <div className="chart-box" style={{ height }}>
      <canvas ref={canvas} role="img" aria-label={onPick ? `${label} (select a bar or slice to see its transactions)` : label} />
    </div>
  )
}
