<script lang="ts">
  import AppLoxone from '$lib/components/AppLoxone.svelte';
  import { route } from 'sv-router/generated';
</script>

<AppLoxone loxone={route.meta.loxoneData?.loxone ?? null} error={route.meta.loxoneData?.error ?? null} />
