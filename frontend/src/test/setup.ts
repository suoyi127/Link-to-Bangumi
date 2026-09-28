import '@testing-library/jest-dom/vitest'
import { configure } from '@testing-library/react'

// Async UI queries can exceed the default one second on a cold Windows CI runner.
configure({ asyncUtilTimeout: 10_000 })
