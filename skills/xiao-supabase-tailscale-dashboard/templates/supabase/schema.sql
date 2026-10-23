-- XIAO device state table for remote LED control + touch status.
-- Replace {{DEVICE_ID}} (e.g. xiao-01). Safe to apply to a project shared with other apps:
-- it grants nothing on any other table.

create table public.xiao_devices (
  id text primary key,
  led boolean not null default false,
  touched boolean not null default false,
  touch_value integer not null default 0,
  threshold integer not null default 0,
  rssi integer not null default 0,
  updated_at timestamptz not null default now()  -- written by the device only (NTP time)
);

alter table public.xiao_devices enable row level security;

-- Dashboard and device share the publishable key: read and update state columns only.
-- No insert/delete, so the public key cannot create or remove devices.
revoke all on public.xiao_devices from anon, authenticated;
grant select on public.xiao_devices to anon, authenticated;
grant update (led, touched, touch_value, threshold, rssi, updated_at) on public.xiao_devices to anon, authenticated;

create policy "xiao_devices public read" on public.xiao_devices
  for select to anon, authenticated using (true);
create policy "xiao_devices public update" on public.xiao_devices
  for update to anon, authenticated using (true) with check (true);

insert into public.xiao_devices (id) values ('{{DEVICE_ID}}');

-- Dashboard receives device changes instantly via Supabase Realtime
alter publication supabase_realtime add table public.xiao_devices;
