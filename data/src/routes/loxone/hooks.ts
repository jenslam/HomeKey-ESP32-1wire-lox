import type { Hooks } from 'sv-router';
import type { LoxoneConfig } from '$lib/types/api';
import { setLoadingState } from '$lib/stores/system.svelte';

declare module 'sv-router' {
  interface RouteMeta {
    loxoneData?: { loxone: LoxoneConfig | null; error: string | null };
  }
}

export default {
  async beforeLoad({ meta }) {
    try {
      setLoadingState(true);
      const res = await fetch('/config?type=loxone').then(r => r.json());
      if (!res.success) throw new Error(res.error);
      meta.loxoneData = { loxone: res.data as LoxoneConfig, error: null };
    } catch (error) {
      console.error('Failed to load loxone config:', error);
      meta.loxoneData = { loxone: null, error: error instanceof Error ? error.message : 'Unknown error' };
    } finally {
      setLoadingState(false);
    }
  },
} satisfies Hooks;
