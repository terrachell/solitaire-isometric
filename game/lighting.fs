#version 330
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragPosition;
in vec3 fragTangent;
in vec3 fragBitangent;

out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec4 colDiffuse;

void main() {
    vec4 texelColor = texture(texture0, fragTexCoord);

    // Скруглённые углы карты в текстуре прозрачные. Пиксели с малой альфой
    // отбрасываем полностью: иначе они попадают в буфер глубины и вырезают
    // карту, которая лежит под ними.
    if (texelColor.a < 0.5) discard;

    vec3 normalMap = texture(texture1, fragTexCoord).rgb;
    vec3 normal = normalMap * 2.0 - 1.0;

    mat3 TBN = mat3(fragTangent, fragBitangent, fragNormal);
    vec3 worldNormal = normalize(TBN * normal);

    vec3 lightDir = normalize(vec3(-0.5, 1.0, 0.5));
    float diff = max(dot(worldNormal, lightDir), 0.0);

    float brightness = 0.85 + (diff * 0.15);

    // Альфу текстуры обязательно пробрасываем в результат (раньше здесь была
    // жёсткая 1.0 — карта рисовалась непрозрачным прямоугольником).
    finalColor = vec4(texelColor.rgb * colDiffuse.rgb * brightness, texelColor.a * colDiffuse.a);
}
