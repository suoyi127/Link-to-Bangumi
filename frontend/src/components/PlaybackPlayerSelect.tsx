import { useEffect, useState } from 'react'
import { Alert, Space } from 'antd'
import { getPlayers } from '../api/client'
import type { PlayerCatalog } from '../api/types'

export function PlaybackPlayerSelect({ value, onChange }: { value: string; onChange: (id: string) => void }) {
  const [catalog, setCatalog] = useState<PlayerCatalog | null>(null)
  const [error, setError] = useState('')
  useEffect(() => {
    const controller = new AbortController()
    void getPlayers(controller.signal).then((result) => { if (!controller.signal.aborted) setCatalog(result) })
      .catch(() => { if (!controller.signal.aborted) setError('播放器列表暂不可用，仍可尝试使用已保存的默认播放器。') })
    return () => controller.abort()
  }, [])
  const name = catalog?.items.find((item) => item.id === catalog.selectedId)?.name
  return <Space direction="vertical">
    <label>本次播放使用 <select aria-label="本次播放器" value={value} onChange={(event) => onChange(event.target.value)}>
      <option value="">默认播放器{name ? `（${name}）` : ''}</option>
      {catalog?.items.map((item) => <option key={item.id} value={item.id} disabled={!item.available}>{item.name}{item.available ? '' : '（未配置）'}</option>)}
    </select></label>
    {error && <Alert type="warning" message={error} />}
  </Space>
}
