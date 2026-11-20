import Chart from 'chart.js/auto'
import type { ChartConfiguration } from 'chart.js'
import { useEffect, useRef } from 'react'

export function ChartCanvas({ config, height = 300, label }: { config: ChartConfiguration; height?: number; label: string }) {
  const canvas = useRef<HTMLCanvasElement>(null)
  useEffect(() => {
    if (!canvas.current) return
    const chart = new Chart(canvas.current, config)
    return () => chart.destroy()
  }, [config])
  return (
    <div className="chart-box" style={{ height }}>
      <canvas ref={canvas} role="img" aria-label={label} />
    </div>
  )
}
