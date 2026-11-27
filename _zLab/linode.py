"""Akamai Cloud v4 client for zLab's managed Linode CPU instances."""
from __future__ import annotations

import http.client
import ipaddress
import json
import math
import re
import urllib.parse

from runpod import MAX_RESPONSE_BYTES, RunPodError, _http_transport


API_ROOT = 'https://api.linode.com/v4'
MAX_PAGES = 100
# The regions API supplies city names but no coordinates. Order known regions
# by great-circle distance from Austin to their city centers (see the zLab guide).
AUSTIN_REGION_ORDER = (
    'us-hou-1', 'us-central', 'mx-qro-1', 'us-den-1', 'us-southeast', 'us-ord',
    'us-mia', 'us-lax', 'us-iad', 'us-iad-2', 'ca-central', 'us-west', 'us-east',
    'us-sea', 'co-bog-1', 'co-bog-2', 'cl-scl-1', 'eu-west', 'gb-lon', 'br-gru',
    'nl-ams', 'es-mad', 'fr-par', 'fr-par-2', 'de-ham-1', 'se-sto', 'de-fra-2',
    'eu-central', 'de-ber-1', 'fr-mrs-2', 'it-mil', 'ap-northeast', 'jp-tyo-3',
    'jp-osa', 'nz-akl-1', 'ap-southeast', 'au-mel', 'ap-west', 'in-bom-2',
    'za-jnb-1', 'in-maa', 'my-kul-1', 'ap-south', 'sg-sin-2', 'id-cgk',
)


class LinodeError(RunPodError):
    """Provider errors safe to display, including uncertain mutation outcomes."""


def _compute_optimized(plan):
    """G8 dedicated exposes both 1:2 and 1:4 shapes under the same class."""
    identity, count, memory = plan.get('id'), plan.get('vcpus'), plan.get('memory')
    return (type(count) is int and count > 0 and type(memory) is int
            and memory == count * 2048 and plan.get('class') == 'dedicated'
            and identity == f'g8-dedicated-{memory // 1024}-{count}')


def _region_id(value):
    return isinstance(value, str) and re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', value)


def _offer(plan, region):
    count = plan['vcpus']
    memory = plan['memory'] // 1024
    selected = {'provider': 'linode', 'region': region, 'memory_gib': memory,
                'cpu': {'id': plan['id'], 'vcpuCount': count}}
    # Region overrides apply to the whole plan. Add-on prices are separate.
    rates = plan.get('region_prices', [])
    if not isinstance(rates, list) or any(not isinstance(rate, dict) for rate in rates):
        raise LinodeError('Linode reported invalid regional pricing.')
    matches = [rate for rate in rates if rate.get('id') == region]
    if len(matches) > 1:
        raise LinodeError('Linode reported conflicting prices for one region.')
    price = matches[0] if matches else plan.get('price')
    hourly = price.get('hourly') if isinstance(price, dict) else None
    if type(hourly) in (int, float) and 0 <= hourly < math.inf:
        cost = f'${hourly:g}/hr'
    else:
        hourly = math.inf
        cost = 'price unknown'
    label = f'Linode G8 Compute Optimized · {count} vCPU / {memory} GiB · {region} · {cost}'
    return label, selected, hourly


def _regional_offers(plan, stock):
    available = {region: _offer(plan, region) for region, in_stock in stock.items() if in_stock}
    if not available:
        return []
    iad = next((region for region in ('us-iad', 'us-iad-2') if region in available), None)
    nearest = next((region for region in AUSTIN_REGION_ORDER if region in available), None)
    selected = {region for region in (iad, 'us-lax', nearest) if region in available}

    # Pick only one additional region for price. Prefer a region already on
    # display when the lowest whole-instance hourly rate is shared.
    cheapest = min(available, key=lambda region: (available[region][2], region not in selected, region))
    if math.isfinite(available[cheapest][2]):
        selected.add(cheapest)

    return [available[region] for region in selected]


def _instance(row):
    """Translate a provider observation. Identity is required even during boot."""
    if (not isinstance(row, dict) or type(row.get('id')) is not int or row['id'] <= 0
            or not isinstance(row.get('label'), str) or not row['label']):
        raise LinodeError('Linode returned an invalid instance identity.')
    specs = row.get('specs')
    specs = specs if isinstance(specs, dict) else {}
    memory = specs.get('memory')
    status = row.get('status')
    cpu = {'id': row.get('type'), 'vcpuCount': specs.get('vcpus'),
           'memory': memory / 1024 if type(memory) is int else None}
    pod = {'id': str(row['id']), 'name': row['label'], 'provider': 'linode',
           'status': status.upper() if isinstance(status, str) else 'UNKNOWN',
           'region': row.get('region'), 'cpu': cpu}
    if specs.get('gpus'):
        pod['gpu'] = {'count': specs['gpus']}
    if pod['status'] == 'RUNNING' and isinstance(row.get('ipv4'), list):
        for address in row['ipv4']:
            if not isinstance(address, str):
                continue
            try:
                ip = ipaddress.IPv4Address(address)
            except ipaddress.AddressValueError:
                continue
            if ip.is_global and not ip.is_multicast:
                pod['ssh'] = {'direct': {'host': str(ip), 'port': 22, 'username': 'root'}}
                break
    return pod


class LinodeClient:
    def __init__(self, token, *, transport=None, timeout=30):
        if not isinstance(token, str) or not token or any(ord(c) < 33 or ord(c) > 126 for c in token):
            raise ValueError('Linode token must be one nonempty printable token without spaces.')
        if type(timeout) not in (int, float) or not 0 < timeout < math.inf:
            raise ValueError('Timeout must be positive and finite.')
        self._token = token
        self._transport = transport or _http_transport
        self._timeout = timeout

    def _request(self, method, path, *, payload=None, query=None):
        # Catalog endpoints are public. Only account instance requests need a token.
        headers = {'Accept': 'application/json'}
        if path.startswith('/linode/instances'):
            headers['Authorization'] = 'Bearer ' + self._token
        url = API_ROOT + path
        if query:
            url += '?' + urllib.parse.urlencode(query)
        body = None
        if payload is not None:
            body = json.dumps(payload, allow_nan=False).encode('utf-8')
            headers['Content-Type'] = 'application/json'
        mutation = method != 'GET'
        try:
            status, response = self._transport(method, url, headers, body, self._timeout)
        except (OSError, http.client.HTTPException):
            raise LinodeError('Linode connection failed or timed out.', uncertain=mutation) from None
        if not 200 <= status < 300:
            hint = {400: 'Check the plan, region and image in Cloud Manager.',
                    401: 'Check the Linode personal access token.',
                    403: 'Check Linodes read/write permission and account access.',
                    429: 'Rate limit reached. Retry later.'}.get(status, '')
            raise LinodeError(f'Linode request failed (HTTP {status}). {hint}'.rstrip(),
                              status=status, uncertain=mutation and (status < 400 or status == 408 or status >= 500))
        if len(response) > MAX_RESPONSE_BYTES:
            raise LinodeError('Linode response exceeded the size limit.', uncertain=mutation)
        if method == 'DELETE' and not response:
            return status, None
        try:
            return status, json.loads(response)
        except (ValueError, RecursionError):
            raise LinodeError('Linode response contained invalid JSON.', uncertain=mutation) from None

    def _collection(self, path):
        rows = []
        expected = None
        for number in range(1, MAX_PAGES + 1):
            _, page = self._request('GET', path, query={'page': number, 'page_size': 500})
            if not isinstance(page, dict):
                raise LinodeError('Linode returned an invalid collection.')
            pages, total, data = page.get('pages'), page.get('results'), page.get('data')
            if (type(page.get('page')) is not int or page['page'] != number
                    or type(pages) is not int or not 0 <= pages <= MAX_PAGES
                    or type(total) is not int or total < 0 or not isinstance(data, list)
                    or len(data) > 500 or any(not isinstance(row, dict) for row in data)
                    or (expected is not None and expected != (pages, total))):
                raise LinodeError('Linode collection changed or had invalid pagination. Refresh and try again.')
            expected = (pages, total)
            rows.extend(data)
            if number >= pages:
                if len(rows) != total or (pages == 0 and total):
                    raise LinodeError('Linode returned an incomplete collection. Refresh and try again.')
                return rows
        raise LinodeError('Linode collection exceeded the pagination limit.')

    def catalog(self):
        """List each placement once, with the lowest-rate 256-vCPU instance first."""
        try:
            plans = {}
            for plan in self._collection('/linode/types'):
                if not _compute_optimized(plan) or 2 < plan['vcpus'] <= 32:
                    continue
                if plan['id'] in plans:
                    raise LinodeError('Linode returned a duplicate plan.')
                plans[plan['id']] = plan
            stock = {identity: {} for identity in plans}
            for row in self._collection('/regions/availability'):
                identity, region = row.get('plan'), row.get('region')
                if not isinstance(identity, str) or identity not in plans or not _region_id(region):
                    continue
                if region in stock[identity]:
                    raise LinodeError('Linode returned conflicting regional availability. Refresh and try again.')
                stock[identity][region] = row.get('available') is True
            offers = []
            for identity, plan in plans.items():
                offers.extend(_regional_offers(plan, stock[identity]))
            offers.sort(key=lambda offer: (offer[1]['cpu']['vcpuCount'] != 256,
                                           -offer[1]['cpu']['vcpuCount'], offer[2], offer[1]['region']))
            return [(label, compute) for label, compute, _ in offers], []
        except LinodeError as error:
            return [], [str(error)]

    def creation_quote(self, compute):
        """Recheck this exact plan and region immediately before confirmation."""
        cpu = compute.get('cpu')
        identity = cpu.get('id') if isinstance(cpu, dict) else None
        region = compute.get('region')
        if (compute.get('provider') != 'linode' or not _region_id(region)
                or not isinstance(identity, str) or not re.fullmatch(r'g8-dedicated-[0-9]+-[0-9]+', identity)):
            raise ValueError('Select an available Linode Compute Optimized plan and region.')
        _, plan = self._request('GET', '/linode/types/' + identity)
        if not isinstance(plan, dict) or not _compute_optimized(plan):
            raise LinodeError('The selected Linode plan has changed. Refresh the catalog.')
        label, fresh, _ = _offer(plan, region)
        if fresh != compute:
            raise LinodeError('Linode no longer reports the selected CPU and memory allocation. Refresh the catalog.')
        _, rows = self._request('GET', f'/regions/{region}/availability')
        if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
            raise LinodeError('Linode did not report availability for the selected region.')
        matches = [row for row in rows if row.get('plan') == identity and row.get('region') == region]
        if len(matches) != 1 or matches[0].get('available') is not True:
            raise LinodeError('The selected Linode plan is unavailable in this region. Refresh the catalog.')
        return label

    @staticmethod
    def _instance_path(identity):
        if not isinstance(identity, str) or not re.fullmatch(r'[1-9][0-9]*', identity):
            raise ValueError('Invalid Linode instance ID.')
        return '/linode/instances/' + identity

    def list_pods(self):
        return [_instance(row) for row in self._collection('/linode/instances')]

    def list_queued(self):
        # A successful create immediately supplies the provisioning instance ID.
        return []

    def get_pod(self, identity):
        _, row = self._request('GET', self._instance_path(identity))
        pod = _instance(row)
        if pod['id'] != identity:
            raise LinodeError('Linode returned a different instance ID.')
        return pod

    def create_pod(self, payload):
        status, row = self._request('POST', '/linode/instances', payload=payload)
        if status not in (200, 201):
            raise LinodeError('Linode returned an unexpected creation status.', status=status, uncertain=True)
        try:
            pod = _instance(row)
        except LinodeError:
            raise LinodeError('Linode did not confirm the new instance identity.', uncertain=True) from None
        if pod['name'] != payload.get('label'):
            raise LinodeError('Linode did not confirm the new instance label.', uncertain=True)
        return 201, pod

    def delete_pod(self, identity):
        self._request('DELETE', self._instance_path(identity))
