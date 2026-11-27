"""Offline contract checks for zLab's Linode v4 provider."""
import json
import unittest
from urllib.parse import parse_qs, urlsplit

from linode import LinodeClient, LinodeError


def plan(count, *, ratio=2, generation=8, price=8.64):
    return {'id': f'g{generation}-dedicated-{count * ratio}-{count}',
            'class': 'dedicated', 'memory': 1024 * count * ratio, 'vcpus': count,
            'price': {'hourly': price, 'monthly': None},
            'region_prices': [{'id': 'br-gru', 'hourly': 12.096, 'monthly': None}]}


def page(data, number=1, pages=1, total=None):
    return {'page': number, 'pages': pages, 'results': len(data) if total is None else total,
            'data': data}


def stock(plan, region='us-iad', available=True):
    return {'plan': plan['id'], 'region': region, 'available': available}


def instance(identity=245):
    return {'id': identity, 'label': 'pump-lab-instance', 'type': plan(256)['id'],
            'status': 'running', 'region': 'us-iad', 'specs': {'vcpus': 256, 'memory': 524288, 'gpus': 0},
            'ipv4': ['192.168.3.4', '172.105.1.1']}


class Transport:
    def __init__(self, *responses):
        self.responses = list(responses)
        self.calls = []

    def __call__(self, method, url, headers, body, timeout):
        self.calls.append((method, url, headers, body))
        response = self.responses.pop(0)
        if isinstance(response, Exception):
            raise response
        status, data = response
        return status, data if isinstance(data, bytes) else json.dumps(data).encode()


class LinodeTests(unittest.TestCase):
    def test_catalog_hides_sizes_from_three_through_thirty_two_vcpus(self):
        plans = [plan(count) for count in (1, 2, 3, 16, 32, 33, 256)]
        fake = Transport((200, page(plans)), (200, page([stock(p) for p in plans])))
        choices, errors = LinodeClient('fixture', transport=fake).catalog()
        self.assertFalse(errors)
        self.assertEqual([compute['cpu']['vcpuCount'] for _, compute in choices], [256, 33, 2, 1])

    def test_rank_exact_g8_shape_available_region_and_whole_instance_price(self):
        large, small, general, legacy = plan(256), plan(48, price=1.6), plan(128, ratio=4), plan(64, generation=7)
        large['region_prices'] = [{'id': 'us-lax', 'hourly': 12.096}]
        fake = Transport((200, page([small, general, legacy, large])),
                         (200, page([stock(small), stock(general), stock(legacy)], pages=2, total=6)),
                         (200, page([stock(large, 'us-lax'), stock(large, 'us-iad'),
                                     stock(large, 'us-east', False)], 2, 2, 6)),
                         (200, dict(large, price={'hourly': 9.9})), (200, [stock(large)]))
        client = LinodeClient('private-token', transport=fake)
        choices, failures = client.catalog()
        self.assertFalse(failures)
        self.assertEqual([compute['cpu']['vcpuCount'] for _, compute in choices], [256, 256, 48])
        large_offers = {compute['region']: (label, compute) for label, compute in choices[:2]}
        iad_label, iad = large_offers['us-iad']
        self.assertIn('$8.64/hr', iad_label)
        self.assertIn('$12.096/hr', large_offers['us-lax'][0])
        self.assertEqual(iad, {'provider': 'linode', 'region': 'us-iad', 'memory_gib': 512,
                               'cpu': {'id': 'g8-dedicated-512-256', 'vcpuCount': 256}})
        self.assertIn('$9.9/hr', client.creation_quote(iad))
        self.assertTrue(all(urlsplit(url).hostname == 'api.linode.com' and not body
                            and 'Authorization' not in headers for _, url, headers, body in fake.calls))
        self.assertEqual(parse_qs(urlsplit(fake.calls[2][1]).query)['page'], ['2'])
        self.assertTrue(fake.calls[-1][1].endswith('/regions/us-iad/availability'))

    def test_region_shortlist_uses_each_shapes_stock_and_global_hourly_prices(self):
        large, small = plan(256), plan(48)
        large['region_prices'] = [{'id': 'br-gru', 'hourly': 1.20},
                                  {'id': 'us-east', 'hourly': 0.90}]
        small['region_prices'] = [{'id': 'de-fra-2', 'hourly': 1.10}]
        both = ('us-iad-2', 'us-lax', 'us-southeast', 'us-ord', 'br-gru', 'de-fra-2')
        inventory = [stock(shape, region) for shape in (large, small) for region in both]
        inventory.extend([stock(large), stock(large, 'us-central'), stock(large, 'us-east', False),
                          stock(small, available=False), stock(small, 'us-central', False)])
        expected = {
            256: ['us-iad', 'us-lax', 'us-central', 'br-gru'],
            48: ['us-iad-2', 'us-lax', 'us-southeast', 'de-fra-2'],
        }
        results = []
        for rows in (inventory, list(reversed(inventory))):
            fake = Transport((200, page([small, large])), (200, page(rows)))
            choices, errors = LinodeClient('fixture', transport=fake).catalog()
            self.assertFalse(errors)
            selected = [(c['cpu']['vcpuCount'], c['region']) for _, c in choices]
            self.assertCountEqual(selected, [(count, r) for count, regions in expected.items() for r in regions])
            self.assertEqual(selected[0], (256, 'br-gru'))
            results.append(selected)
        self.assertEqual(results[0], results[1])

    def test_overlapping_regions_and_price_ties_produce_one_row_per_destination(self):
        large = plan(256)
        rows = [stock(large, region) for region in ('de-fra-2', 'us-iad-2', 'us-lax', 'gb-lon')]
        rows.append(stock(large, 'us-iad', False))
        default_regions = []
        for inventory in (rows, list(reversed(rows))):
            fake = Transport((200, page([large])), (200, page(inventory)))
            choices, errors = LinodeClient('fixture', transport=fake).catalog()
            self.assertFalse(errors)
            self.assertCountEqual([c['region'] for _, c in choices], ['us-iad-2', 'us-lax'])
            for label, compute in choices:
                self.assertEqual(label, 'Linode G8 Compute Optimized · 256 vCPU / 512 GiB · '
                                 + compute['region'] + ' · $8.64/hr')
            default_regions.append(choices[0][1]['region'])
        self.assertEqual(default_regions[0], default_regions[1])

    def test_stocked_houston_is_closer_to_austin_than_dallas(self):
        large = plan(256)
        rows = [stock(large, r) for r in ('us-central', 'us-hou-1', 'us-lax')]
        fake = Transport((200, page([large])), (200, page(rows)))
        choices, errors = LinodeClient('fixture', transport=fake).catalog()
        self.assertFalse(errors)
        self.assertCountEqual([c['region'] for _, c in choices], ['us-hou-1', 'us-lax'])

    def test_default_when_256_vcpu_is_out_of_stock(self):
        large, medium, small = plan(256), plan(128, price=None), plan(48, price=0.8)
        medium['region_prices'] = [{'id': 'us-lax', 'hourly': 4.32}]
        rows = [stock(small), stock(large, available=False), stock(medium), stock(medium, 'us-lax')]
        fake = Transport((200, page([small, large, medium])), (200, page(rows)))
        choices, errors = LinodeClient('fixture', transport=fake).catalog()
        self.assertFalse(errors)
        self.assertEqual([(c['cpu']['vcpuCount'], c['region']) for _, c in choices],
                         [(128, 'us-lax'), (128, 'us-iad'), (48, 'us-iad')])

    def test_unknown_rates_cannot_supply_a_cheapest_region_but_a_quoted_zero_can(self):
        large = plan(256, price=None)
        rows = [stock(large, r) for r in ('unknown-site-a', 'unknown-site-b')]
        for hourly in (None, -1, '0', 0):
            with self.subTest(hourly=hourly):
                large['region_prices'] = [{'id': 'unknown-site-b', 'hourly': hourly}]
                fake = Transport((200, page([large])), (200, page(rows)))
                choices, _ = LinodeClient('fixture', transport=fake).catalog()
                self.assertEqual([c['region'] for _, c in choices],
                                 ['unknown-site-b'] if hourly == 0 else [])
                if hourly == 0:
                    self.assertIn('$0/hr', choices[0][0])

    def test_quote_requires_current_requested_region_availability_and_shape(self):
        large = plan(256)
        compute = {'provider': 'linode', 'region': 'us-iad', 'memory_gib': 512,
                   'cpu': {'id': large['id'], 'vcpuCount': 256}}
        for region_stock in ([], [stock(large, 'us-east')], [stock(large, available=False)],
                             [stock(large), stock(large, available=False)]):
            with self.subTest(stock=region_stock):
                client = LinodeClient('token', transport=Transport((200, large), (200, region_stock)))
                with self.assertRaisesRegex(LinodeError, 'unavailable'):
                    client.creation_quote(compute)
        for changed in (plan(128), dict(large, memory=4096), dict(large, **{'class': 'shared'})):
            with self.subTest(plan=changed), self.assertRaises(LinodeError):
                LinodeClient('token', transport=Transport((200, changed))).creation_quote(compute)

    def test_incomplete_inventory_and_duplicate_stock_are_rejected(self):
        row = instance()
        for pages in ([(200, page([], total=1))], [(200, page([row], pages=2, total=2)),
                                                   (200, page([row], number=1, pages=2, total=2))]):
            with self.subTest(pages=pages), self.assertRaises(LinodeError):
                LinodeClient('token', transport=Transport(*pages)).list_pods()
        large = plan(256)
        fake = Transport((200, page([large])), (200, page([stock(large), stock(large, available=False)])))
        choices, failures = LinodeClient('token', transport=fake).catalog()
        self.assertFalse(choices)
        self.assertTrue(failures)

    def test_managed_instance_creation_connection_observation_list_and_delete(self):
        running = instance()
        booting = dict(running, status='provisioning')
        fake = Transport((200, booting), (200, running), (200, page([running])), (200, {}))
        client = LinodeClient('private-token', transport=fake)
        create = {'type': running['type'], 'region': running['region'], 'label': running['label'],
                  'image': 'linode/ubuntu24.04', 'authorized_keys': ['test-public-key'],
                  'metadata': {'user_data': 'private-bootstrap'}}
        status, pod = client.create_pod(create)
        self.assertEqual(status, 201)
        self.assertEqual(pod['status'], 'PROVISIONING')
        self.assertNotIn('ssh', pod)
        self.assertEqual(pod['id'], '245')
        ready = client.get_pod('245')
        self.assertEqual(ready['cpu'], {'id': running['type'], 'vcpuCount': 256, 'memory': 512})
        self.assertEqual(ready['ssh']['direct'], {'host': '172.105.1.1', 'port': 22, 'username': 'root'})
        self.assertEqual(client.list_pods(), [ready])
        self.assertEqual(client.list_queued(), [])
        client.delete_pod('245')
        self.assertEqual(json.loads(fake.calls[0][3]), create)
        self.assertEqual([call[0] for call in fake.calls], ['POST', 'GET', 'GET', 'DELETE'])
        self.assertTrue(all(headers['Authorization'] == 'Bearer private-token' for _, _, headers, _ in fake.calls))
        self.assertNotIn('private-bootstrap', repr(pod))

    def test_provider_evidence_required_and_invalid_id_never_used(self):
        row = instance()
        del row['specs']
        row['ipv4'] = ['192.168.3.4', '127.0.0.1', '169.254.1.1']
        fake = Transport((200, row), (200, dict(row, id=246)))
        client = LinodeClient('token', transport=fake)
        pod = client.get_pod('245')
        self.assertIsNone(pod['cpu']['vcpuCount'])
        self.assertIsNone(pod['cpu']['memory'])
        self.assertNotIn('ssh', pod)
        with self.assertRaisesRegex(LinodeError, 'different instance ID'):
            client.get_pod('245')
        for identity in ('../2', '0', '2/3', 245):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                client.delete_pod(identity)
        self.assertEqual(len(fake.calls), 2)

    def test_ambiguous_create_is_never_retried_and_errors_do_not_echo_secrets(self):
        payload = {'label': 'pump-lab-instance', 'metadata': {'user_data': 'private-bootstrap'}}
        for response, uncertain in ((TimeoutError('private-token'), True), ((408, {}), True),
                                    ((200, b'private-bootstrap'), True), ((200, {'id': 245}), True),
                                    ((200, dict(instance(), label='wrong')), True), ((503, {}), True),
                                    ((400, {'errors': [{'reason': 'private-bootstrap'}]}), False),
                                    ((403, {}), False), ((302, {}), True)):
            fake = Transport(response)
            with self.subTest(response=response), self.assertRaises(LinodeError) as caught:
                LinodeClient('private-token', transport=fake).create_pod(payload)
            self.assertEqual(len(fake.calls), 1)
            self.assertEqual(caught.exception.uncertain, uncertain)
            self.assertNotIn('private', str(caught.exception))


if __name__ == '__main__':
    unittest.main()
