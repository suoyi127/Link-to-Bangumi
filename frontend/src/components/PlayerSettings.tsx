import { useEffect, useState } from 'react'
import { Alert, Button, Input, Space, Typography } from 'antd'
import { getPlayers, savePlayer } from '../api/client'
import type { PlayerCatalog } from '../api/types'

export function PlayerSettings() {
  const [catalog, setCatalog] = useState<PlayerCatalog | null>(null)
  const [selected, setSelected] = useState('mpv')
  const [path, setPath] = useState('')
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')
  const [busy, setBusy] = useState(false)
  const [refresh, setRefresh] = useState(0)
  useEffect(() => {
    const controller = new AbortController()
    setError(''); setCatalog(null)
    void getPlayers(controller.signal).then((result) => {
      if (controller.signal.aborted) return
      setCatalog(result); setSelected(result.selectedId)
      setPath(result.items.find((item) => item.id === result.selectedId)?.executable ?? '')
    }).catch((cause) => { if (!controller.signal.aborted) setError(cause instanceof Error ? cause.message : '播放器检测失败') })
    return () => controller.abort()
  }, [refresh])
  async function save() {
    setBusy(true); setError(''); setNotice('')
    try {
      const result = await savePlayer(selected, selected === 'system' ? '' : path.trim())
      setCatalog(result); setPath(result.items.find((item) => item.id === selected)?.executable ?? '')
      setNotice('默认播放器已保存，播放时仍可临时切换。')
    } catch (cause) { setError(cause instanceof Error ? cause.message : '播放器保存失败') }
    finally { setBusy(false) }
  }
  return <Space direction="vertical" style={{ width: '100%' }}>
    <Typography.Title level={4}>本机播放器</Typography.Title>
    <Typography.Text type="secondary">选择默认播放器；未检测到的安装版或便携版可填写 exe 绝对路径，不会自动安装软件。</Typography.Text>
    {error && <Alert type="error" message={error} />}
    {notice && <Alert type="success" message={notice} />}
    {catalog && <>
      <label>默认播放器 <select aria-label="默认播放器" value={selected} disabled={busy} onChange={(event) => {
        setSelected(event.target.value); setPath(catalog.items.find((item) => item.id === event.target.value)?.executable ?? ''); setNotice('')
      }}>{catalog.items.map((item) => <option key={item.id} value={item.id}>{item.name}{item.id === 'system' ? '' : item.available ? '（已检测到）' : '（需指定路径）'}</option>)}</select></label>
      {selected !== 'system' && <Input aria-label="播放器可执行文件" value={path} maxLength={1024} disabled={busy} placeholder="便携版播放器的 exe 绝对路径" onChange={(event) => { setPath(event.target.value); setNotice('') }} />}
      <Typography.Text type="secondary">系统默认使用 Windows 文件关联；自定义播放器必须支持接收单个媒体文件路径。</Typography.Text>
      <Button type="primary" loading={busy} onClick={() => void save()}>保存默认播放器</Button>
    </>}
    <Button disabled={busy} onClick={() => { setRefresh((value) => value + 1); setNotice('') }}>重新检测播放器</Button>
  </Space>
}
