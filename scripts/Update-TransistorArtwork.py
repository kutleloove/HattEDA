"""Regenerate schematic artwork from terminal roles without moving persisted pins.

Authoring helper only; the application reads embedded JSON, never invokes Python.
"""
import json
import math
from pathlib import Path

catalog_path = Path(__file__).resolve().parents[1] / 'libs/ui-shell/resources/library/builtin.json'
catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
for device in catalog['devices']:
    model = device.get('simulationModel', '')
    if model not in ('nonlinear.bjt-npn', 'nonlinear.bjt-pnp', 'nonlinear.nmos', 'nonlinear.pmos'):
        continue
    mosfet = model in ('nonlinear.nmos', 'nonlinear.pmos')
    if device['id'] == 'lib.transistor.npn':
        for pin, name in zip(device['pins'], ('B', 'C', 'E')):
            pin['name'] = name
            pin['type'] = 'input' if name == 'B' else 'passive'
    names = [pin['name'] for pin in device['pins']]
    roles = ('D', 'G', 'S') if mosfet else ('C', 'B', 'E')
    for variant in device['variants']:
        output, control, return_pin = (variant['pinPositions'][names.index(role)] for role in roles)
        # Keep the internal symbol on cardinal axes; only the external leads
        # adapt to the historical pin offsets.
        angle = 0 if control[0] < -abs(control[1]) else (math.pi / 2 if control[1] < 0 else -math.pi / 2)

        def rotate(point):
            x, y = point
            return [round(x * math.cos(angle) - y * math.sin(angle), 6),
                    round(x * math.sin(angle) + y * math.cos(angle), 6)]

        def line(*points):
            return {'points': [rotate(point) for point in points]}

        # Choose the output branch nearest its fixed pin; this also handles packaged
        # pin permutations without swapping pin identities or copper pad mappings.
        upper = rotate((2.54, -2.8))
        lower = rotate((2.54, 2.8))
        branch = -1 if math.dist(upper, output) <= math.dist(lower, output) else 1
        other = -branch
        circle = {'closed': True, 'points': [[round(4.2 * math.cos(i * math.tau / 48), 6),
                                            round(4.2 * math.sin(i * math.tau / 48), 6)] for i in range(48)]}
        control_x = -1.5 if mosfet else -0.635
        shapes = [circle, {'points': [control, rotate((control_x, 0))]},
                  line((control_x, -1.905), (control_x, 1.905))]
        if mosfet:
            # Insulated gate and three broken enhancement-channel segments.
            shapes.extend(line((0.635, a), (0.635, b)) for a, b in ((-1.905, -0.8), (-0.5, 0.5), (0.8, 1.905)))
            shapes.extend([{'points': [rotate((0.635, branch * 1.27)), rotate((2.54, branch * 2.8)), output]},
                           {'points': [rotate((0.635, other * 1.27)), rotate((2.54, other * 2.8)), return_pin]},
                           line((2.54, other * 2.8), (2.54, 0), (0.635, 0))])
            direction = -1 if model == 'nonlinear.nmos' else 1
            arrow = [(1.55 + direction * 0.55, 0), (1.55 - direction * 0.35, -0.4),
                     (1.55 - direction * 0.35, 0.4)]
        else:
            shapes.extend([{'points': [rotate((-0.635, branch * 0.8)), rotate((2.54, branch * 2.8)), output]},
                           {'points': [rotate((-0.635, other * 0.8)), rotate((2.54, other * 2.8)), return_pin]}])
            start, end = (-0.635, other * 0.8), (2.54, other * 2.8)
            length = math.dist(start, end)
            ux, uy = ((end[i] - start[i]) / length for i in range(2))
            sign = 1 if model == 'nonlinear.bjt-npn' else -1
            mx, my = (start[i] + (end[i] - start[i]) * 0.65 for i in range(2))
            arrow = [(mx + sign * ux * 0.65, my + sign * uy * 0.65),
                     (mx - sign * ux * 0.4 - uy * 0.4, my - sign * uy * 0.4 + ux * 0.4),
                     (mx - sign * ux * 0.4 + uy * 0.4, my - sign * uy * 0.4 - ux * 0.4)]
        shapes.append({'closed': True, 'filled': True, 'points': [rotate(p) for p in arrow]})
        variant['shapes'] = shapes

generated = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
if catalog_path.read_text(encoding='utf-8') != generated:
    catalog_path.write_text(generated, encoding='utf-8')
