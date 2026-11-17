import { access } from 'node:fs/promises';
import { resolve } from 'node:path';
import { expect, it } from 'vitest';

it('has no obsolete ZIP-only packaging entry points', async () => {
  for (const name of ['pack.sh', 'scripts/pack-windows.mjs', 'tests/pack-windows.test.mjs'])
    await expect(access(resolve(import.meta.dirname, '..', name))).rejects.toMatchObject({ code: 'ENOENT' });
});
