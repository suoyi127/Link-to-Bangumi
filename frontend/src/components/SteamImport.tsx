import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Input, Space, Typography } from 'antd'
import { getSteamConfig, getSteamLogin, importSteamGames, saveSteamConfig, startSteamLogin } from '../api/steam'
import { ApiError } from '../api/request'
import { steamErrorText, steamWarningText } from '../api/steamMessages'

export function SteamImport({ onImported, disabled }: { onImported: () => Promise<void>; disabled: boolean }) {
  const [attempt, setAttempt] = useState(0)
  const [ready, setReady] = useState(false)
  const [steamPath, setSteamPath] = useState('')
  const [steamId, setSteamId] = useState('')
  const [apiKey, setApiKey] = useState('')
  const [hasApiKey, setHasApiKey] = useState(false)
  const [dirty, setDirty] = useState(false)
  const [busy, setBusy] = useState<'save' | 'import' | 'login' | ''>('')
  const lock = useRef(false)
  const loginController = useRef<AbortController | null>(null)
  const loginTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)
  const [error, setError] = useState('')
  const [message, setMessage] = useState('')
  const [warnings, setWarnings] = useState<string[]>([])
  useEffect(() => () => {
    loginController.current?.abort()
    clearTimeout(loginTimer.current)
  }, [])
  useEffect(() => {
    const controller = new AbortController()
    void getSteamConfig(controller.signal).then((config) => {
      if (controller.signal.aborted) return
      setSteamPath(config.steamPath)
      setSteamId(config.steamId)
      setHasApiKey(config.hasApiKey)
      setReady(true)
    }).catch((error: unknown) => {
      if (!controller.signal.aborted) setError(steamErrorText(error))
    })
    return () => controller.abort()
  }, [attempt])
  async function login() {
    if (lock.current || dirty || !ready || disabled) return
    lock.current = true
    setBusy('login')
    setError('')
    setMessage('')
    setWarnings([])
    const controller = new AbortController()
    loginController.current = controller
    const deadline = Date.now() + 15 * 60 * 1000
    const finish = () => { lock.current = false; setBusy('') }
    const fail = (error: unknown) => {
      if (controller.signal.aborted) return
      setError(steamErrorText(error))
      finish()
    }
    async function poll(sessionId: string) {
      if (controller.signal.aborted) return
      if (Date.now() >= deadline) {
        setError('')
        setMessage('Steam 登录已超时，请重新登录。')
        finish()
        return
      }
      try {
        let status
        try {
          status = await getSteamLogin(sessionId, controller.signal)
        } catch (error) {
          if (controller.signal.aborted) return
          if (error instanceof ApiError && error.code === 'steam_login_not_found') throw error
          // 短暂断线不会结束原生登录会话，继续跟踪同一会话直至超时。
          setError('Steam 连接暂时中断，正在重新连接。')
          loginTimer.current = setTimeout(() => void poll(sessionId), 1000)
          return
        }
        if (controller.signal.aborted) return
        setError('')
        if (status.state === 'pending') {
          // 上次请求结束后才安排下一次轮询，避免请求重叠。
          loginTimer.current = setTimeout(() => void poll(sessionId), 1000)
          return
        }
        if (status.state === 'completed') {
          let config
          try {
            config = await getSteamConfig(controller.signal)
          } catch {
            if (controller.signal.aborted) return
            setError('Steam 连接暂时中断，正在重新连接。')
            loginTimer.current = setTimeout(() => void poll(sessionId), 1000)
            return
          }
          if (controller.signal.aborted) return
          setSteamPath(config.steamPath)
          setSteamId(config.steamId)
          setHasApiKey(config.hasApiKey)
          setApiKey('')
          setDirty(false)
          setMessage('Steam 登录完成，配置已自动保存。')
        } else if (status.state === 'cancelled') {
          setMessage('Steam 登录已取消，可以重新登录。')
        } else if (status.state === 'expired') {
          setMessage('Steam 登录已超时，请重新登录。')
        } else {
          throw new ApiError(status.errorCode || 'steam_login_failed', 400)
        }
        finish()
      } catch (error) { fail(error) }
    }
    try {
      const session = await startSteamLogin(controller.signal)
      if (controller.signal.aborted) return
      loginTimer.current = setTimeout(() => void poll(session.sessionId), 1000)
    } catch (error) { fail(error) }
  }
  async function run(mode: 'save' | 'import') {
    if (lock.current) return
    lock.current = true
    setBusy(mode)
    setError('')
    setMessage('')
    setWarnings([])
    try {
      if (mode === 'save') {
        const config = await saveSteamConfig({ steamPath: steamPath.trim(), steamId: steamId.trim(), apiKey: apiKey.trim() })
        setHasApiKey(config.hasApiKey)
        // 密钥只保留在当前输入中，保存成功即清空，不写入浏览器存储。
        setApiKey('')
        setDirty(false)
        setMessage('Steam 设置已保存。')
      } else {
        const result = await importSteamGames()
        setMessage(`Steam 导入完成：新增 ${result.imported}，更新 ${result.updated}。`)
        setWarnings(result.warnings)
        await onImported()
      }
    } catch (error) {
      setError(steamErrorText(error))
    } finally {
      lock.current = false
      setBusy('')
    }
  }
  const blocked = disabled || !!busy || !ready
  return <Space direction="vertical" style={{ width: '100%' }}>
    <Button type="primary" aria-label="登录 Steam 并自动配置" disabled={blocked || dirty} loading={busy === 'login'} onClick={() => void login()}>登录 Steam 并自动配置</Button>
    <Typography.Text type="secondary">在 Steam 官方窗口登录后自动保存账号配置；首次申请 API Key 时，请在窗口内完成注册与确认。</Typography.Text>
    {busy === 'login' && <Typography.Text role="status">请在 Steam 官方窗口完成登录和确认，完成后这里会自动更新。</Typography.Text>}
    <details>
      <summary style={{ cursor: 'pointer' }}>Steam 导入设置</summary>
      <Space direction="vertical" style={{ width: '100%', marginTop: 12 }}>
        <Typography.Text type="secondary">读取本机已安装的 Steam 游戏。路径留空自动查找；Steam ID 和 API Key 可选，不填写也可导入本地游戏。</Typography.Text>
        <label>Steam 安装目录<Input aria-label="Steam 安装目录" value={steamPath} disabled={blocked} placeholder="留空自动查找" onChange={(event) => { setSteamPath(event.target.value); setDirty(true) }} /></label>
        <label>Steam ID（64 位数字）<Input aria-label="Steam ID" value={steamId} disabled={blocked} onChange={(event) => { setSteamId(event.target.value); setDirty(true) }} /></label>
        <label>Steam API Key<Input.Password aria-label="Steam API Key" value={apiKey} disabled={blocked} autoComplete="new-password" onChange={(event) => { setApiKey(event.target.value); setDirty(true) }} /></label>
        {ready && <Typography.Text>{hasApiKey ? '已保存 API Key；留空保留原密钥' : '尚未保存 API Key'}</Typography.Text>}
        <Typography.Paragraph type="secondary">同步游玩时长与成就需要 Steam ID、<a href="https://steamcommunity.com/dev/apikey" target="_blank" rel="noreferrer">Steam Web API Key</a>，以及可公开访问的个人资料和游戏详情。请勿分享密钥；隐私设置或网络问题会导致统计暂不可用。</Typography.Paragraph>
        <Button disabled={blocked} loading={busy === 'save'} onClick={() => void run('save')}>保存 Steam 设置</Button>
      </Space>
    </details>
    <Button aria-label={busy === 'import' ? '正在导入并同步 Steam 统计' : '一键导入 Steam 游戏'} disabled={blocked || dirty} loading={busy === 'import'} onClick={() => void run('import')}>{busy === 'import' ? '正在导入并同步 Steam 统计' : '一键导入 Steam 游戏'}</Button>
    {dirty && <Typography.Text type="secondary">请先保存 Steam 设置，再登录或开始导入。</Typography.Text>}
    {!ready && error && <Button onClick={() => { setError(''); setAttempt((value) => value + 1) }}>重试加载 Steam 设置</Button>}
    {error && <Alert type="error" message={error} />}
    {message && <Alert type="info" message={message} />}
    {warnings.map((warning, index) => <Alert key={`${index}-${warning}`} type="warning" message={steamWarningText(warning)} />)}
  </Space>
}
