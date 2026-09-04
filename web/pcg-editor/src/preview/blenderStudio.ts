import * as THREE from 'three';

/** Blender's bundled `release/datafiles/studiolights/studio/studio.sl`. */
export const BLENDER_STUDIO_LIGHTS = [
  {
    diffuse: [0.723042, 0.723042, 0.723042],
    specular: [0.685956, 0.685956, 0.685956],
    direction: [-0.854701, 0.111111, 0.507091],
    smooth: 0.2,
  },
  {
    diffuse: [0.0631, 0.069978, 0.067951],
    specular: [0.145797, 0.162642, 0.157673],
    direction: [0.058607, -0.987943, -0.143295],
    smooth: 0.719626,
  },
  {
    diffuse: [0.157432, 0.163405, 0.214035],
    specular: [0.246195, 0.225308, 0.225308],
    direction: [0.972202, 0.075846, -0.221518],
    smooth: 0.28125,
  },
] as const;

export const BLENDER_SOLID_COLOR = 0.8;
const BLENDER_SOLID_ROUGHNESS = Math.sqrt(0.4);

function glslVec3(value: readonly [number, number, number]): string {
  return `vec3(${value.join(', ')})`;
}

function wrappedLighting(nl: number, smooth: number): number {
  const divisor = (smooth + 1) ** 2;
  return Math.max(0, Math.min(1, (nl + smooth) / divisor));
}

/** CPU oracle used by the material-contract check. */
export function evaluateBlenderStudioDiffuse(normal: readonly [number, number, number]): [number, number, number] {
  const result: [number, number, number] = [0, 0, 0];
  for (const light of BLENDER_STUDIO_LIGHTS) {
    const nl = light.direction[0] * normal[0]
      + light.direction[1] * normal[1]
      + light.direction[2] * normal[2];
    const factor = wrappedLighting(nl, light.smooth);
    result[0] += light.diffuse[0] * factor;
    result[1] += light.diffuse[1] * factor;
    result[2] += light.diffuse[2] * factor;
  }
  return result;
}

export function createBlenderStudioMaterial(vertexColors: boolean): THREE.ShaderMaterial {
  const lightConstants = BLENDER_STUDIO_LIGHTS.map((light, index) => `
    const vec3 LIGHT_${index}_DIRECTION = ${glslVec3(light.direction)};
    const vec3 LIGHT_${index}_DIFFUSE = ${glslVec3(light.diffuse)};
    const vec3 LIGHT_${index}_SPECULAR = ${glslVec3(light.specular)};
    const float LIGHT_${index}_SMOOTH = ${light.smooth};`).join('\n');
  const vertexColorDeclaration = vertexColors ? 'attribute vec4 color; varying vec3 vColor;' : '';
  const vertexColorAssignment = vertexColors ? 'vColor = color.rgb;' : '';
  const fragmentColorDeclaration = vertexColors ? 'varying vec3 vColor;' : '';
  const baseColor = vertexColors ? 'vColor' : `vec3(${BLENDER_SOLID_COLOR})`;

  const material = new THREE.ShaderMaterial({
    name: 'Blender Workbench Studio',
    uniforms: {
      opacity: { value: 1 },
    },
    vertexShader: `
      #include <common>
      #include <skinning_pars_vertex>

      ${vertexColorDeclaration}
      varying vec3 vNormalView;
      varying vec3 vViewPosition;

      void main() {
        #include <beginnormal_vertex>
        #include <skinbase_vertex>
        #include <skinnormal_vertex>
        #include <defaultnormal_vertex>

        #include <begin_vertex>
        #include <skinning_vertex>

        vec4 viewPosition = modelViewMatrix * vec4(transformed, 1.0);
        vNormalView = normalize(transformedNormal);
        vViewPosition = viewPosition.xyz;
        ${vertexColorAssignment}
        gl_Position = projectionMatrix * viewPosition;
      }
    `,
    fragmentShader: `
      uniform float opacity;
      ${fragmentColorDeclaration}
      varying vec3 vNormalView;
      varying vec3 vViewPosition;
      ${lightConstants}
      const float ROUGHNESS = ${BLENDER_SOLID_ROUGHNESS};

      float wrapped_lighting(float nl, float smoothness) {
        float width = smoothness + 1.0;
        return clamp((nl + smoothness) / (width * width), 0.0, 1.0);
      }

      vec3 brdf_approx(vec3 specularColor, float roughness, float nv) {
        float fresnel = exp2(-8.35 * nv) * (1.0 - roughness);
        return mix(specularColor, vec3(1.0), fresnel);
      }

      float blender_specular(vec3 lightDirection, vec3 incoming, vec3 normal, vec3 reflection,
                             float roughness, float smoothness) {
        float nl = max(dot(lightDirection, normal), 0.0);
        float specularAngle = max(dot(normalize(lightDirection + incoming), normal), 0.0);
        float gloss = (1.0 - roughness) * (1.0 - smoothness);
        float shininess = exp2(10.0 * gloss + 1.0);
        float direct = pow(specularAngle, shininess) * nl * (shininess * 0.125 + 1.0);
        float width = mix(smoothness, 1.0, roughness);
        float environment = wrapped_lighting(dot(lightDirection, reflection), width);
        return mix(direct, environment, smoothness * smoothness);
      }

      void accumulate_light(vec3 lightDirection, vec3 diffuseColor, vec3 specularColor,
                            float smoothness, vec3 incoming, vec3 normal, vec3 reflection,
                            inout vec3 diffuseLight, inout vec3 specularLight) {
        diffuseLight += diffuseColor * wrapped_lighting(dot(lightDirection, normal), smoothness);
        specularLight += specularColor * blender_specular(
          lightDirection, incoming, normal, reflection, ROUGHNESS, smoothness);
      }

      void main() {
        vec3 normal = normalize(vNormalView);
        if (!gl_FrontFacing) normal = -normal;
        vec3 incoming = normalize(-vViewPosition);
        vec3 reflection = -reflect(incoming, normal);
        vec3 diffuseLight = vec3(0.0);
        vec3 specularLight = vec3(0.0);

        accumulate_light(LIGHT_0_DIRECTION, LIGHT_0_DIFFUSE, LIGHT_0_SPECULAR, LIGHT_0_SMOOTH,
                         incoming, normal, reflection, diffuseLight, specularLight);
        accumulate_light(LIGHT_1_DIRECTION, LIGHT_1_DIFFUSE, LIGHT_1_SPECULAR, LIGHT_1_SMOOTH,
                         incoming, normal, reflection, diffuseLight, specularLight);
        accumulate_light(LIGHT_2_DIRECTION, LIGHT_2_DIFFUSE, LIGHT_2_SPECULAR, LIGHT_2_SMOOTH,
                         incoming, normal, reflection, diffuseLight, specularLight);

        vec3 specularColor = brdf_approx(vec3(0.05), ROUGHNESS, max(dot(normal, incoming), 0.0));
        float specularEnergy = dot(specularColor, vec3(0.33333));
        vec3 litColor = diffuseLight * ${baseColor} * (1.0 - specularEnergy)
          + specularLight * specularColor;
        gl_FragColor = vec4(litColor, opacity);
        #include <colorspace_fragment>
      }
    `,
    side: THREE.DoubleSide,
    toneMapped: false,
  });
  return material;
}
